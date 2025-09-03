#include "elog_stats.h"

#include <cinttypes>

#include "elog_aligned_alloc.h"
#include "elog_internal.h"
#include "elog_report.h"
#include "elog_stats_internal.h"
#include "elog_target.h"
#include "elog_tls.h"

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogStats)

/** @def Thread shutting down constant. */
#define ELOG_SHUTDOWN_STAT_SLOT_ID ((uint64_t)-2)

static thread_local uint64_t sThreadSlotId = ELOG_INVALID_STAT_SLOT_ID;
static ELogTlsKey sStatTlsKey = ELOG_INVALID_TLS_KEY;

static std::atomic<uint64_t>* sThreadSlots = nullptr;
static uint64_t sMaxThreads = ELOG_DEFAULT_MAX_THREADS;
static uint64_t allocThreadSlotId();
static void freeThreadSlotId(uint64_t slotId);

static void cleanUpSlotId(void* key) {
    // NOTE: in MinGW, apparently at this point we can still access sThreadSlotId
    // NOTE: initially we put (slotId + 1) to ensure tls dtor get triggered

    sThreadSlotId = ELOG_SHUTDOWN_STAT_SLOT_ID;
    uint64_t slotId = ((uint64_t)key) - 1;
    // NOTE: this internal logger call can trigger call to allocThreadSlotId(), so we first set
    // sThreadSlotId to ELOG_SHUTDOWN_STAT_SLOT_ID, so that the call to allocThreadSlotId() would
    // reject the request
    ELOG_REPORT_TRACE(
        "Cleanup statistics slot called for current thread with key %p slot id %" PRIu64, key,
        slotId);

    // NOTE: we do not reset counters, since some other thread might want to check statistics,
    // instead statistics are reset during slot allocation

    freeThreadSlotId(slotId);
    // NOTE: we keep the thread local value as ELOG_SHUTDOWN_STAT_SLOT_ID so that it will not get
    // allocated again in case other TLS cleanup code triggers a slot request
}

bool initializeStats(uint32_t maxThreads) {
    if (!elogCreateTls(sStatTlsKey, cleanUpSlotId)) {
        ELOG_REPORT_ERROR("Failed to initialize log target statistics TLS key");
        return false;
    }
    sThreadSlots = elogAlignedAllocObjectArray<std::atomic<uint64_t>>(ELOG_CACHE_LINE, maxThreads,
                                                                      (uint64_t)0);
    if (sThreadSlots == nullptr) {
        ELOG_REPORT_ERROR("Failed to allocate statistics slot array for %u threads, out of memory",
                          maxThreads);
        elogDestroyTls(sStatTlsKey);
        sStatTlsKey = ELOG_INVALID_TLS_KEY;
        return false;
    }
    sMaxThreads = maxThreads;
    return true;
}

void terminateStats() {
    if (sThreadSlots != nullptr) {
        elogAlignedFreeObjectArray<std::atomic<uint64_t>>(sThreadSlots, sMaxThreads);
        sThreadSlots = nullptr;
    }
    if (sStatTlsKey != ELOG_INVALID_TLS_KEY) {
        elogDestroyTls(sStatTlsKey);
    }
}

uint64_t allocThreadSlotId() {
    // if we are during cleanup of slot, we reject any request to allocated the slot
    if (sThreadSlotId == ELOG_SHUTDOWN_STAT_SLOT_ID) {
        return ELOG_INVALID_STAT_SLOT_ID;
    }

    // search for any vacant slot
    uint64_t slotId = ELOG_INVALID_STAT_SLOT_ID;
    for (uint32_t i = 0; i < sMaxThreads; ++i) {
        uint64_t value = sThreadSlots[i].load(std::memory_order_acquire);
        if (value == 0) {
            if (sThreadSlots[i].compare_exchange_strong(value, 1, std::memory_order_seq_cst)) {
                slotId = i;
                break;
            }
        }
    }

    // save slot in thread local var
    if (slotId != ELOG_INVALID_STAT_SLOT_ID) {
        sThreadSlotId = slotId;
        ELOG_REPORT_TRACE("Allocated statistics thread slot id %" PRIu64, sThreadSlotId);
        // NOTE: we do not reset thread counters since that may cause wrong reporting, as the total
        // message count for a log target may suddenly drop (when it can only increase) due to
        // thread resetting its counters
    }
    return slotId;
}

void freeThreadSlotId(uint64_t slotId) {
    ELOG_REPORT_TRACE("Freeing statistics thread slot id %" PRIu64, slotId);
    sThreadSlots[slotId].store(0, std::memory_order_seq_cst);
}

bool ELogStatVar::initialize(uint32_t maxThreads) {
    m_threadCounters = elogAlignedAllocObjectArray<ELogCounter>(ELOG_CACHE_LINE, maxThreads);
    if (m_threadCounters == nullptr) {
        ELOG_REPORT_ERROR(
            "Failed to allocate statistics variable counter array for %u threads, out of memory",
            maxThreads);
        return false;
    }
    m_maxThreads = maxThreads;
    return true;
}

void ELogStatVar::terminate() {
    if (m_threadCounters != nullptr) {
        elogAlignedFreeObjectArray<ELogCounter>(m_threadCounters, m_maxThreads);
        m_threadCounters = nullptr;
    }
}

bool ELogStats::initialize(uint32_t maxThreads) {
    if (!m_msgDiscarded.initialize(maxThreads) || !m_msgSubmitted.initialize(maxThreads) ||
        !m_msgWritten.initialize(maxThreads) || !m_msgFailWrite.initialize(maxThreads) ||
        !m_bytesSubmitted.initialize(maxThreads) || !m_bytesWritten.initialize(maxThreads) ||
        !m_bytesFailWrite.initialize(maxThreads) || !m_flushSubmitted.initialize(maxThreads) ||
        !m_flushExecuted.initialize(maxThreads) || !m_flushFailed.initialize(maxThreads) ||
        !m_flushDiscarded.initialize(maxThreads)) {
        ELOG_REPORT_ERROR("Failed to initialize statistics variables");
        terminate();
        return false;
    }

    return true;
}

void ELogStats::terminate() {
    m_msgDiscarded.terminate();
    m_msgSubmitted.terminate();
    m_msgWritten.terminate();
    m_msgFailWrite.terminate();

    m_bytesSubmitted.terminate();
    m_bytesWritten.terminate();
    m_bytesFailWrite.terminate();

    m_flushSubmitted.terminate();
    m_flushExecuted.terminate();
    m_flushFailed.terminate();
    m_flushDiscarded.terminate();
}

void ELogStats::toString(ELogBuffer& buffer, ELogTarget* logTarget, const char* msg /* = "" */) {
    if (msg != nullptr && *msg != 0) {
        buffer.appendArgs("%s (log target: %s/%s):\n", msg, logTarget->getTypeName(),
                          logTarget->getName());
    } else {
        buffer.appendArgs("Statistics for log target %s/%s:\n", logTarget->getTypeName(),
                          logTarget->getName());
    }

#define PRINT_STAT(var, msg)                                    \
    {                                                           \
        uint64_t sum = var.getSum();                            \
        if (sum > 0) {                                          \
            buffer.appendArgs("\t" msg ": %" PRIu64 "\n", sum); \
        }                                                       \
    }

    PRINT_STAT(m_msgDiscarded, "Log messages discarded");
    PRINT_STAT(m_msgSubmitted, "Log messages submitted");
    PRINT_STAT(m_msgWritten, "Log messages written");
    PRINT_STAT(m_msgFailWrite, "Log messages failed write");

    PRINT_STAT(m_bytesSubmitted, "Bytes submitted");
    PRINT_STAT(m_bytesWritten, "Bytes written");
    PRINT_STAT(m_bytesFailWrite, "Bytes failed write");

    PRINT_STAT(m_flushSubmitted, "Flush requests submitted");
    PRINT_STAT(m_flushExecuted, "Flush requests executed");
    PRINT_STAT(m_flushFailed, "Flush requests failed write");
    PRINT_STAT(m_flushDiscarded, "Flush requests discarded");
}

uint64_t ELogStats::getSlotId() {
    uint64_t slotId = sThreadSlotId;
    if (slotId == ELOG_INVALID_STAT_SLOT_ID) {
        slotId = allocThreadSlotId();
        if (slotId != ELOG_INVALID_STAT_SLOT_ID) {
            // NOTE: if tls value is null for the current thread, then dtor is not triggered, so we
            // want to any non-value here, but the problem with gcc (at least on MinGW), is that by
            // the time we reach the TLS dtor function, the thread_local variable sThreadSlotId is
            // already reset to initial value. so we must put the slot id in the tls key for cleanup
            // purposes. also we add +1 to avoid putting zero/null, otherwise on Linux/MinGW the
            // destructor function will not be called
            elogSetTls(sStatTlsKey, (void*)(slotId + 1));
        } else {
            ELOG_REPORT_WARN(
                "Attempt to allocates statistics slot for current thread failed, probable cause: "
                "number of active threads exceeds the number configured during initialization: %u",
                sMaxThreads);
        }
    }
    return sThreadSlotId;
}

void ELogStats::resetThreadCounters(uint64_t slotId) {
    m_msgDiscarded.reset(slotId);
    m_msgSubmitted.reset(slotId);
    m_msgWritten.reset(slotId);
    m_msgFailWrite.reset(slotId);

    m_bytesSubmitted.reset(slotId);
    m_bytesWritten.reset(slotId);
    m_bytesFailWrite.reset(slotId);

    m_flushSubmitted.reset(slotId);
    m_flushExecuted.reset(slotId);
    m_flushFailed.reset(slotId);
    m_flushDiscarded.reset(slotId);
}

}  // namespace elog