#include "elog_target.h"

#include "elog_common.h"
#include "elog_filter.h"
#include "elog_flush_policy.h"
#include "elog_formatter.h"
#include "elog_system.h"

namespace elog {

bool ELogTarget::start() {
    if (m_requiresLock) {
        std::unique_lock<std::recursive_mutex> lock(m_lock);
        return startNoLock();
    } else {
        return startNoLock();
    }
}

bool ELogTarget::startNoLock() {
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
    if (m_flushPolicy != nullptr) {
        if (!m_flushPolicy->stop()) {
            return false;
        }
    }
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
        if (shouldFlush(bytesWritten)) {
            flush();
        }
    }
}

uint32_t ELogTarget::writeLogRecord(const ELogRecord& logRecord) {
    // default implementation - format log message and write to log
    // this might not suite all targets, as formatting might take place on a later phase
    // NOTE: this is a naive attempt to reuse a pre-allocated string buffer for better performance
    static thread_local std::string logMsg(ELOG_DEFAULT_LOG_MSG_RESERVE_SIZE, 0);
    logMsg.clear();  // does this deallocate??
    assert(logMsg.capacity() > 0);
    formatLogMsg(logRecord, logMsg);
    logFormattedMsg(logMsg);
    return logMsg.length();
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
        logMsg += "\n";
    }
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
