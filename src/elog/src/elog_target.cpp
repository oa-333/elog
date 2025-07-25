#include "elog_target.h"

#include <random>

#include "elog.h"
#include "elog_aligned_alloc.h"
#include "elog_common.h"
#include "elog_filter.h"
#include "elog_flush_policy.h"
#include "elog_formatter.h"
#include "elog_internal.h"
#include "elog_report.h"
#include "elog_tls.h"

namespace elog {

static ELogTlsKey sLogBufferKey = ELOG_INVALID_TLS_KEY;

inline ELogBuffer* allocLogBuffer() {
    // ELogBuffer* logBuffer = new (std::align_val_t(ELOG_CACHE_LINE), std::nothrow) ELogBuffer();
    return elogAlignedAllocObject<ELogBuffer>(ELOG_CACHE_LINE);
}

inline void freeLogBuffer(void* data) {
    ELogBuffer* logBuffer = (ELogBuffer*)data;
    if (logBuffer != nullptr) {
        //::operator delete(logBuffer, std::align_val_t(ELOG_CACHE_LINE));
        elogAlignedFreeObject(logBuffer);
    }
}

static ELogBuffer* getOrCreateTlsLogBuffer() {
    ELogBuffer* logBuffer = (ELogBuffer*)elogGetTls(sLogBufferKey);
    if (logBuffer == nullptr) {
        logBuffer = allocLogBuffer();
        if (logBuffer == nullptr) {
            ELOG_REPORT_ERROR("Failed to allocate thread-local log buffer");
            return nullptr;
        }
        if (!elogSetTls(sLogBufferKey, logBuffer)) {
            ELOG_REPORT_ERROR("Failed to set thread-local log buffer");
            //::operator delete(logBuffer, std::align_val_t(ELOG_CACHE_LINE));
            freeLogBuffer(logBuffer);
            return nullptr;
        }
    }
    return logBuffer;
}

ELogTarget::~ELogTarget() {
    setLogFilter(nullptr);
    setLogFormatter(nullptr);
    setFlushPolicy(nullptr);
    if (m_stats != nullptr) {
        delete m_stats;
        m_stats = nullptr;
    }
}

bool ELogTarget::createLogBufferKey() {
    if (sLogBufferKey != ELOG_INVALID_TLS_KEY) {
        ELOG_REPORT_ERROR("Cannot create log buffer TLS key, already created");
        return false;
    }
    return elogCreateTls(sLogBufferKey, freeLogBuffer);
}

bool ELogTarget::destroyLogBufferKey() {
    if (sLogBufferKey == ELOG_INVALID_TLS_KEY) {
        // silently ignore the request
        return true;
    }
    bool res = elogDestroyTls(sLogBufferKey);
    if (res) {
        sLogBufferKey = ELOG_INVALID_TLS_KEY;
    }
    return res;
}

bool ELogTarget::start() {
    if (m_requiresLock) {
        std::unique_lock<std::recursive_mutex> lock(m_lock);
        return startNoLock();
    } else {
        return startNoLock();
    }
}

bool ELogTarget::startNoLock() {
    bool isRunning = m_isRunning.load(std::memory_order_relaxed);
    if (isRunning || !m_isRunning.compare_exchange_strong(isRunning, true)) {
        ELOG_REPORT_ERROR("Cannot start log target %s/%s, already running", m_typeName.c_str(),
                          m_name.c_str());
        return false;
    }
    if (m_enableStats && m_stats == nullptr) {
        m_stats = createStats();
        if (m_stats == nullptr) {
            ELOG_REPORT_ERROR("Cannot start log target %s/%s, failed to create statistics object",
                              m_typeName.c_str(), m_name.c_str());
            return false;
        }
        m_stats->initialize(elog::getMaxThreads());
    }
    if (m_flushPolicy != nullptr) {
        if (!m_flushPolicy->start()) {
            if (m_stats != nullptr) {
                delete m_stats;
                m_stats = nullptr;
            }
            return false;
        }
    }
    bool res = startLogTarget();
    if (!res) {
        // TODO: is it ok to delete flush policy here? what is the contract? should it be deleted
        // only during dtor? or it doesn't really matter? this seems out of order
        delete m_flushPolicy;
        m_flushPolicy = nullptr;
        if (m_stats != nullptr) {
            delete m_stats;
            m_stats = nullptr;
        }
    }
    return res;
}

bool ELogTarget::stop() {
    if (m_requiresLock) {
        std::unique_lock<std::recursive_mutex> lock(m_lock);
        return stopNoLock();
    } else {
        return stopNoLock();
    }
}

bool ELogTarget::stopNoLock() {
    bool isRunning = m_isRunning.load(std::memory_order_relaxed);
    if (!isRunning || !m_isRunning.compare_exchange_strong(isRunning, false)) {
        ELOG_REPORT_ERROR("Cannot stop log target %s/%s, not running");
        return false;
    }
    if (m_flushPolicy != nullptr) {
        if (!m_flushPolicy->stop()) {
            return false;
        }
    }
    flushLogTarget();
    bool res = stopLogTarget();
    if (res) {
        if (m_stats != nullptr) {
            ELogBuffer buffer;
            m_stats->toString(buffer, this, "Log target statistics during stop");
            ELOG_REPORT_TRACE("Log target statistics during stop:\n%s", buffer.getRef());
        }
    }
    return res;
}

void ELogTarget::log(const ELogRecord& logRecord) {
    if (m_requiresLock) {
        std::unique_lock<std::recursive_mutex> lock(m_lock);
        logNoLock(logRecord);
    } else {
        logNoLock(logRecord);
    }
}

void ELogTarget::logNoLock(const ELogRecord& logRecord) {
    uint64_t slotId = m_enableStats ? m_stats->getSlotId() : ELOG_INVALID_STAT_SLOT_ID;
    if (!canLog(logRecord)) {
        if (m_enableStats) {
            m_stats->incrementMsgDiscarded(slotId);
        }
        return;
    }

    if (m_enableStats) {
        m_stats->incrementMsgSubmitted(slotId);
    }

    // write log record
    uint32_t bytesWritten = writeLogRecord(logRecord);

    // update statistics counter
    if (m_enableStats) {
        m_stats->incrementMsgWritten(slotId);
        m_stats->addBytesWritten(slotId, bytesWritten);
    }

    // NOTE: asynchronous log targets return zero here, but log flushing of the end target will
    // be triggered by the async log target anyway
    // NOTE: we call shouldFlush() anyway, even if zero bytes were returned, since some flush
    // policies don't care how many bytes were written, but rather how many calls were made
    if (m_flushPolicy != nullptr) {
        // NOTE: we pass to shouldFlush() the currently logged message size
        if (m_flushPolicy->shouldFlush(bytesWritten)) {
            // don't call flush(), but rather flushNoLock() - we already have the lock
            // also allow flush moderation to take place if needed
            flushNoLock(true);
        }
    }
}

ELogStats* ELogTarget::createStats() {
    ELogStats* res = new (std::nothrow) ELogStats();
    if (res == nullptr) {
        ELOG_REPORT_ERROR("Failed to create statistics object, out of memory");
    }
    return res;
}

uint32_t ELogTarget::writeLogRecord(const ELogRecord& logRecord) {
    // default implementation - format log message and write to log
    // this might not suite all targets, as formatting might take place on a later phase

    // NOTE: in order to accelerate performance a pre-allocated string buffer used

    // NOTE: On MinGW, when using the thread_local keyword, this leads to a crash due to static/TLS
    // destruction problems. Specifically, it crashes during TLS destruction, and the call stack
    // shows that the logMsg object area is already filled with dead land, meaning it already got
    // destroyed (seems that as a static object during thread exit). Moreover, this crash seems to
    // happen only on debug builds. The probable conclusion is that, the object is destroyed twice
    // when running in release mode, but it doesn't crash since the first destruction does not fill
    // the object area with dead land, and so the second destruction finds an empty object and does
    // nothing. This is mere chance, and this behavior might change in the future

    // For this reason a unified thread local storage was developed, which relies on pthread calls
    ELogBuffer* logBuffer = getOrCreateTlsLogBuffer();
    if (logBuffer == nullptr) {
        return 0;
    }
    logBuffer->reset();
    formatLogBuffer(logRecord, *logBuffer);
    uint32_t bufferSize = logBuffer->getOffset();
    if (m_enableStats) {
        m_stats->addBytesSubmitted(bufferSize);
    }
    logFormattedMsg(logBuffer->getRef(), bufferSize);
    return bufferSize;
}

bool ELogTarget::flush(bool allowModeration /* = false */) {
    if (m_requiresLock) {
        std::unique_lock<std::recursive_mutex> lock(m_lock);
        return flushNoLock(allowModeration);
    } else {
        return flushNoLock(allowModeration);
    }
}

void ELogTarget::statsToString(ELogBuffer& buffer, const char* msg /* = "" */) {
    m_stats->toString(buffer, this, msg);
}

bool ELogTarget::flushNoLock(bool allowModeration) {
    // flush moderation should take place only when log target is natively thread-safe
    // NOTE: Being externally thread safe means that either there is an external lock, or that only
    // one thread accesses the log target - in either case, flush moderation is not required
    uint64_t slotId = m_enableStats ? m_stats->getSlotId() : ELOG_INVALID_STAT_SLOT_ID;
    if (m_enableStats) {
        m_stats->incrementFlushSubmitted(slotId);
    }
    bool res = false;
    if (m_isNativelyThreadSafe && allowModeration) {
        res = m_flushPolicy->moderateFlush(this);
    } else {
        res = flushLogTarget();
    }

    if (m_enableStats) {
        if (res) {
            m_stats->incrementFlushExecuted(slotId);
        } else {
            m_stats->incrementFlushFailed(slotId);
        }
    }
    return res;
}

void ELogTarget::setLogFilter(ELogFilter* logFilter) {
    if (m_logFilter != nullptr) {
        delete m_logFilter;
    }
    m_logFilter = logFilter;
}

void ELogTarget::setLogFormatter(ELogFormatter* logFormatter) {
    if (m_logFormatter != nullptr) {
        delete m_logFormatter;
    }
    m_logFormatter = logFormatter;
}

void ELogTarget::setFlushPolicy(ELogFlushPolicy* flushPolicy) {
    if (m_flushPolicy != nullptr) {
        delete m_flushPolicy;
    }
    m_flushPolicy = flushPolicy;
}

void ELogTarget::formatLogMsg(const ELogRecord& logRecord, std::string& logMsg) {
    if (m_logFormatter != nullptr) {
        m_logFormatter->formatLogMsg(logRecord, logMsg);
    } else {
        elog::formatLogMsg(logRecord, logMsg);
    }
    if (m_addNewLine) {
        logMsg.append("\n");
    }
}

void ELogTarget::formatLogBuffer(const ELogRecord& logRecord, ELogBuffer& logBuffer) {
    if (m_logFormatter != nullptr) {
        m_logFormatter->formatLogBuffer(logRecord, logBuffer);
    } else {
        elog::formatLogBuffer(logRecord, logBuffer);
    }
    if (m_addNewLine) {
        logBuffer.append("\n");
    }
    logBuffer.finalize();
}

bool ELogTarget::canLog(const ELogRecord& logRecord) {
    return logRecord.m_logLevel <= m_logLevel &&
           (m_logFilter == nullptr || m_logFilter->filterLogRecord(logRecord));
}

ELogPassKey ELogTarget::generatePassKey() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dist(1, UINT32_MAX);
    return dist(gen);
}

bool ELogCombinedTarget::startLogTarget() {
    for (ELogTarget* target : m_logTargets) {
        if (isExternallyThreadSafe()) {
            target->setExternallyThreadSafe();
        }
        if (!target->start()) {
            return false;
        }
    }
    return true;
}

bool ELogCombinedTarget::stopLogTarget() {
    for (ELogTarget* target : m_logTargets) {
        if (!target->stop()) {
            return false;
        }
    }
    return true;
}

uint32_t ELogCombinedTarget::writeLogRecord(const ELogRecord& logRecord) {
    // uint32_t bytesWritten = 0;
    if (logRecord.m_logLevel <= getLogLevel()) {
        for (ELogTarget* target : m_logTargets) {
            target->log(logRecord);
        }
    }
    // TODO: what should happen here? how do we make sure each target's flush policy operates
    // correctly?
    return 0;
}

bool ELogCombinedTarget::flushLogTarget() {
    bool res = true;
    for (ELogTarget* target : m_logTargets) {
        if (!target->flush()) {
            res = false;
        }
    }
    return res;
}

}  // namespace elog
