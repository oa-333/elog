#include "elog_target.h"

#include <random>

#include "elog_aligned_alloc.h"
#include "elog_common.h"
#include "elog_error.h"
#include "elog_filter.h"
#include "elog_flush_policy.h"
#include "elog_formatter.h"
#include "elog_system.h"
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
        ELOG_REPORT_ERROR("Cannot start log target %s/%s, already running");
        return false;
    }
    if (m_flushPolicy != nullptr) {
        if (!m_flushPolicy->start()) {
            return false;
        }
    }
    return startLogTarget();
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
    return stopLogTarget();
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
    if (canLog(logRecord)) {
        uint32_t bytesWritten = writeLogRecord(logRecord);
        // update statistics counter
        // NOTE: asynchronous log targets return zero here, but log flushing of the end target will
        // be triggered by the async log target anyway
        // NOTE: we call shouldFlush() anyway, even if zero bytes were returned, since some flush
        // policies don't care how many bytes were written, but rather how many calls were made
        // TODO: check that async target flush policy works correctly
        bytesWritten = m_bytesWritten.fetch_add(bytesWritten, std::memory_order_relaxed);
        if (m_flushPolicy != nullptr) {
            if (m_flushPolicy->shouldFlush(bytesWritten)) {
                // flush moderation should take place only when log target is natively thread-safe
                // NOTE: Being externally thread safe means that either there is an external lock,
                // or that only one thread accesses the log target - in either case, flush
                // moderation is not required
                if (m_isNativelyThreadSafe) {
                    m_flushPolicy->moderateFlush(this);
                } else {
                    // don't call flush(), but rather flushLogTarget() - we already have the lock
                    flushLogTarget();
                }
            }
        }
    }
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
    logFormattedMsg(logBuffer->getRef(), logBuffer->getOffset());
    return logBuffer->getOffset();
}

void ELogTarget::flush() {
    if (m_requiresLock) {
        std::unique_lock<std::recursive_mutex> lock(m_lock);
        return flushLogTarget();
    } else {
        return flushLogTarget();
    }
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
        ELogSystem::formatLogMsg(logRecord, logMsg);
    }
    if (m_addNewLine) {
        logMsg.append("\n");
    }
}

void ELogTarget::formatLogBuffer(const ELogRecord& logRecord, ELogBuffer& logBuffer) {
    if (m_logFormatter != nullptr) {
        m_logFormatter->formatLogBuffer(logRecord, logBuffer);
    } else {
        ELogSystem::formatLogBuffer(logRecord, logBuffer);
    }
    if (m_addNewLine) {
        logBuffer.append("\n");
    }
    logBuffer.finalize();
}

ELogPassKey ELogTarget::generatePassKey() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(1, UINT32_MAX);
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
    uint32_t bytesWritten = 0;
    if (logRecord.m_logLevel <= getLogLevel()) {
        for (ELogTarget* target : m_logTargets) {
            target->log(logRecord);
        }
    }
    // TODO: what should happen here? how do we make sure each target's flush policy operates
    // correctly?
    return 0;
}

void ELogCombinedTarget::flushLogTarget() {
    for (ELogTarget* target : m_logTargets) {
        target->flush();
    }
}

}  // namespace elog
