#include "elog_comm_util_log_handler.h"

#ifdef ELOG_USING_COMM_UTIL

#include <algorithm>
#include <string>

#include "elog_api.h"
#include "elog_common.h"
#include "elog_config_parser.h"
#include "elog_field_selector_internal.h"
#include "elog_report.h"

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogCommUtilLogHandler)

inline ELogLevel severityToLogLevel(commutil::LogSeverity severity) {
    // values are aligned (on purpose), so direct cast can be used
    return (ELogLevel)severity;
}

inline commutil::LogSeverity logLevelToSeverity(ELogLevel logLevel) {
    // values are aligned (on purpose), so direct cast can be used
    return (commutil::LogSeverity)logLevel;
}

commutil::LogSeverity ELogCommUtilLogHandler::onRegisterLogger(commutil::LogSeverity severity,
                                                               const char* loggerName,
                                                               size_t loggerId) {
    // define a log source
    std::string qualifiedLoggerName = std::string("commutil.") + loggerName;
    ELogReportLogger* logger = new (std::nothrow) ELogReportLogger(qualifiedLoggerName.c_str());
    if (logger == nullptr) {
        ELOG_REPORT_ERROR("Failed to allocate logger, out of memory");
        return severity;
    }

    // force early creation of the log source, so that configuration by environment name from
    // elog.cpp is made possible by calling refresh
    if (!logger->initialize()) {
        ELOG_REPORT_ERROR("Failed to initialize logger, internal error");
        return severity;
    }

    // save logger in map (not thread-safe, but this takes place during init phase, so it is ok)
    if (loggerId >= m_commUtilLoggers.size()) {
        m_commUtilLoggers.resize(loggerId + 1);
    }
    m_commUtilLoggers[loggerId] = logger;

    // check for logger log level
    std::string envVarName = qualifiedLoggerName + "_log_level";
    std::replace(envVarName.begin(), envVarName.end(), '.', '_');
    std::string envVarValue;
    if (elog_getenv(envVarName.c_str(), envVarValue)) {
        ELogLevel logLevel = ELEVEL_INFO;
        ELogPropagateMode propagateMode = ELogPropagateMode::PM_NONE;
        if (!ELogConfigParser::parseLogLevel(envVarValue.c_str(), logLevel, propagateMode)) {
            ELOG_REPORT_ERROR("Invalid commutil source %s log level: %s",
                              qualifiedLoggerName.c_str(), envVarValue.c_str());
        } else {
            // we first set logger severity (in the end we will deal with propagation)
            ELOG_REPORT_TRACE("Setting %s initial log level to %s (no propagation)",
                              qualifiedLoggerName.c_str(), elogLevelToStr(logLevel),
                              (uint32_t)propagateMode);
            ELogSource* logSource = logger->getLogger()->getLogSource();
            logSource->setLogLevel(logLevel, propagateMode);
            severity = logLevelToSeverity(logLevel);
            m_logLevelCfg.push_back({logSource, logLevel, propagateMode, loggerId, severity});
        }
    }

    return severity;
}

void ELogCommUtilLogHandler::onUnregisterLogger(size_t loggerId) {
    if (loggerId < m_commUtilLoggers.size()) {
        if (m_commUtilLoggers[loggerId] != nullptr) {
            delete m_commUtilLoggers[loggerId];
            m_commUtilLoggers[loggerId] = nullptr;
        }
        size_t maxLoggerId = m_commUtilLoggers.size() - 1;
        bool done = false;
        while (!done) {
            if (m_commUtilLoggers[maxLoggerId] != nullptr) {
                m_commUtilLoggers.resize(maxLoggerId + 1);
                done = true;
            } else if (maxLoggerId == 0) {
                // last one is also null
                m_commUtilLoggers.clear();
                done = true;
            } else {
                --maxLoggerId;
            }
        }
    }
}

void ELogCommUtilLogHandler::onMsg(commutil::LogSeverity severity, size_t loggerId,
                                   const char* loggerName, const char* msg) {
    // locate logger by id
    if (loggerId < m_commUtilLoggers.size()) {
        ELogReportLogger* reportLogger = m_commUtilLoggers[loggerId];
        if (reportLogger != nullptr) {
            ELogLevel logLevel = severityToLogLevel(severity);
            ELogReport::report(*reportLogger, logLevel, "", 0, "", msg);
        }
    }
}

void ELogCommUtilLogHandler::onThreadStart(const char* threadName) {
    setCurrentThreadNameField(threadName);
}

void ELogCommUtilLogHandler::applyLogLevelCfg() {
    // now we can apply log level propagation
    for (const ELogCommLevelCfg& cfg : m_logLevelCfg) {
        ELOG_REPORT_TRACE("Setting %s log level to %s (propagate - %u)",
                          cfg.m_logSource->getQualifiedName(), elogLevelToStr(cfg.m_logLevel),
                          (uint32_t)cfg.m_propagationMode);
        cfg.m_logSource->setLogLevel(cfg.m_logLevel, cfg.m_propagationMode);
        commutil::setLoggerSeverity(cfg.m_loggerId, cfg.m_severity);
    }
}

void ELogCommUtilLogHandler::refreshLogLevelCfg() {
    for (uint32_t loggerId = 0; loggerId < m_commUtilLoggers.size(); ++loggerId) {
        ELogReportLogger* logger = m_commUtilLoggers[loggerId];
        commutil::setLoggerSeverity(
            loggerId, logLevelToSeverity(logger->getLogger()->getLogSource()->getLogLevel()));
    }
}

}  // namespace elog

#endif  // ELOG_USING_COMM_UTIL
