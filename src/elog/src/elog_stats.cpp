#include "elog_stats.h"

#include <cinttypes>

#include "elog_aligned_alloc.h"
#include "elog_internal.h"
#include "elog_report.h"
#include "elog_stats_internal.h"
#include "elog_target.h"
#include "elog_tls.h"

namespace elog {

static thread_local uint64_t sThreadSlotId = ELOG_INVALID_STAT_SLOT_ID;
static ELogTlsKey sStatTlsKey = ELOG_INVALID_TLS_KEY;

static std::atomic<uint64_t>* sThreadSlots = nullptr;
static uint64_t sMaxThreads = ELOG_DEFAULT_MAX_THREADS;
static uint64_t allocThreadSlotId();
static void freeThreadSlotId(uint64_t slotId);

static void cleanUpSlotId(void* key) {
    // NOTE: in gcc, at this point we cannot access sThreadSlotId, so we must use key
    // NOTE: initially we put (slotId + 1) to ensure tls dtor get triggered
    uint64_t slotId = ((uint64_t)key) - 1;
    ELOG_REPORT_TRACE(
        "Cleanup statistics slot called for current thread with key %p slot id %" PRIu64, key,
        sThreadSlotId);
    elog::resetThreadStatCounters(slotId);
    freeThreadSlotId(slotId);
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
    ELOG_REPORT_TRACE("Allocated statistics thread slot id %" PRIu64, slotId);
    return slotId;
}

void freeThreadSlotId(uint64_t slotId) {
    ELOG_REPORT_TRACE("Freeing statistics thread slot id %" PRIu64, slotId);
    sThreadSlots[slotId].store(0, std::memory_order_relaxed);
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

    buffer.appendArgs("\tLog messages discarded: %" PRIu64 "\n", m_msgDiscarded.getSum());
    buffer.appendArgs("\tLog messages submitted: %" PRIu64 "\n", m_msgSubmitted.getSum());
    buffer.appendArgs("\tLog messages written: %" PRIu64 "\n", m_msgWritten.getSum());
    buffer.appendArgs("\tLog messages failed write: %" PRIu64 "\n", m_msgFailWrite.getSum());

    // buffer.appendArgs("Bytes discarded: %" PRIu64,
    //             m_bytesDiscarded.getSum());
    buffer.appendArgs("\tBytes submitted: %" PRIu64 "\n", m_bytesSubmitted.getSum());
    buffer.appendArgs("\tBytes written: %" PRIu64 "\n", m_bytesWritten.getSum());
    buffer.appendArgs("\tBytes failed write: %" PRIu64 "\n", m_bytesFailWrite.getSum());

    buffer.appendArgs("\tFlush requests submitted: %" PRIu64 "\n", m_flushSubmitted.getSum());
    buffer.appendArgs("\tFlush requests executed: %" PRIu64 "\n", m_flushExecuted.getSum());
    buffer.appendArgs("\tFlush requests failed execution: %" PRIu64 "\n", m_flushFailed.getSum());
    buffer.appendArgs("\tFlush requests discarded: %" PRIu64 "\n", m_flushDiscarded.getSum());
}

uint64_t ELogStats::getSlotId() {
    if (sThreadSlotId == ELOG_INVALID_STAT_SLOT_ID) {
        sThreadSlotId = allocThreadSlotId();
        if (sThreadSlotId != ELOG_INVALID_STAT_SLOT_ID) {
            // NOTE: if tls value is null for the current thread, then dtor is not triggered, so we
            // want to any non-value here, but the problem with gcc (at least on MinGW), is that by
            // the time we reach the TLS dtor function, the thread_local variable sThreadSlotId is
            // already reset to initial value. so we must put the slot id in the tls key for cleanup
            // purposes. also we add +1 to avoid putting zero/null, otherwise on Linux/MinGW the
            // destructor function will not be called
            elogSetTls(sStatTlsKey, (void*)(sThreadSlotId + 1));
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