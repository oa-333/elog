#include "elog_target.h"

#include "elog_filter.h"
#include "elog_flush_policy.h"
#include "elog_formatter.h"
#include "elog_system.h"

namespace elog {

void ELogTarget::log(const ELogRecord& logRecord) {
    // partially implement logging - filter, format, log and flush
    // this might not suite all targets, as formatting might take place on a later phase
    if (shouldLog(logRecord)) {
        std::string logMsg;
        formatLogMsg(logRecord, logMsg);
        logFormattedMsg(logMsg);
        if (shouldFlush(logMsg)) {
            flush();
        }
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

bool ELogTarget::shouldLog(const ELogRecord& logRecord) {
    return logRecord.m_logLevel <= m_logLevel && ELogSystem::filterLogMsg(logRecord);
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

bool ELogTarget::shouldFlush(const std::string& logMsg) {
    return m_flushPolicy == nullptr || m_flushPolicy->shouldFlush(logMsg.length());
}

bool ELogCombinedTarget::start() {
    for (ELogTarget* target : m_logTargets) {
        if (!target->start()) {
            return false;
        }
    }
    return true;
}

bool ELogCombinedTarget::stop() {
    for (ELogTarget* target : m_logTargets) {
        if (!target->stop()) {
            return false;
        }
    }
    return true;
}

void ELogCombinedTarget::log(const ELogRecord& logRecord) {
    if (logRecord.m_logLevel <= getLogLevel()) {
        for (ELogTarget* target : m_logTargets) {
            target->log(logRecord);
        }
    }
}

void ELogCombinedTarget::flush() {
    for (ELogTarget* target : m_logTargets) {
        target->flush();
    }
}

}  // namespace elog
