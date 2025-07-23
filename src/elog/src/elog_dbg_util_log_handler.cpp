#include "elog_dbg_util_log_handler.h"

#ifdef ELOG_ENABLE_STACK_TRACE

#include <algorithm>
#include <string>

#include "elog.h"
#include "elog_common.h"
#include "elog_config_parser.h"
#include "elog_report.h"

namespace elog {

inline ELogLevel severityToLogLevel(dbgutil::LogSeverity severity) {
    // values are aligned (on purpose), so direct cast can be used
    return (ELogLevel)severity;
}

inline dbgutil::LogSeverity logLevelToSeverity(ELogLevel logLevel) {
    // values are aligned (on purpose), so direct cast can be used
    return (dbgutil::LogSeverity)logLevel;
}

dbgutil::LogSeverity ELogDbgUtilLogHandler::onRegisterLogger(dbgutil::LogSeverity severity,
                                                             const char* loggerName,
                                                             size_t loggerId) {
    // define a log source
    std::string qualifiedLoggerName = std::string("dbgutil.") + loggerName;
    ELogSource* logSource = elog::defineLogSource(qualifiedLoggerName.c_str(), true);
    logSource->setModuleName("dbgutil");
    ELogLogger* logger = logSource->createSharedLogger();

    // save logger in map (not thread-safe, but this takes place during init phase, so it is ok)
    if (loggerId >= m_dbgUtilLoggers.size()) {
        m_dbgUtilLoggers.resize(loggerId + 1);
    }
    m_dbgUtilLoggers[loggerId] = logger;

    // check for logger log level
    std::string envVarName = qualifiedLoggerName + "_log_level";
    std::replace(envVarName.begin(), envVarName.end(), '.', '_');
    std::string envVarValue;
    if (elog_getenv(envVarName.c_str(), envVarValue)) {
        ELogLevel logLevel = ELEVEL_INFO;
        ELogPropagateMode propagateMode = ELogPropagateMode::PM_NONE;
        if (!ELogConfigParser::parseLogLevel(envVarValue.c_str(), logLevel, propagateMode)) {
            ELOG_REPORT_ERROR("Invalid dbgutil source %s log level: %s",
                              qualifiedLoggerName.c_str(), envVarValue.c_str());
        } else {
            // we first set logger severity (in the end we will deal with propagation)
            ELOG_REPORT_TRACE("Setting %s initial log level to %s (no propagation)",
                              logSource->getQualifiedName(), elogLevelToStr(logLevel),
                              (uint32_t)propagateMode);
            logSource->setLogLevel(logLevel, propagateMode);
            severity = logLevelToSeverity(logLevel);
            m_logLevelCfg.push_back({logSource, logLevel, propagateMode, loggerId, severity});
        }
    }

    return severity;
}

void ELogDbgUtilLogHandler::onUnregisterLogger(size_t loggerId) {
    if (loggerId < m_dbgUtilLoggers.size()) {
        m_dbgUtilLoggers[loggerId] = nullptr;
        size_t maxLoggerId = m_dbgUtilLoggers.size() - 1;
        bool done = false;
        while (!done) {
            if (m_dbgUtilLoggers[maxLoggerId] != nullptr) {
                m_dbgUtilLoggers.resize(maxLoggerId + 1);
                done = true;
            } else if (maxLoggerId == 0) {
                // last one is also null
                m_dbgUtilLoggers.clear();
                done = true;
            } else {
                --maxLoggerId;
            }
        }
    }
}

void ELogDbgUtilLogHandler::onMsg(dbgutil::LogSeverity severity, size_t loggerId,
                                  const char* loggerName, const char* msg) {
    // locate logger by id
    if (loggerId < m_dbgUtilLoggers.size()) {
        ELogLogger* logger = m_dbgUtilLoggers[loggerId];
        if (logger != nullptr) {
            ELogLevel logLevel = severityToLogLevel(severity);
            if (logger->canLog(logLevel)) {
                logger->logNoFormat(logLevel, "", 0, "", msg);
            } else {
                ELOG_REPORT_TRACE("Discarded dbgutil log source %s message %s, severity %u",
                                  logger->getLogSource()->getQualifiedName(), msg,
                                  (unsigned)severity);
            }
        }
    }
}

void ELogDbgUtilLogHandler::applyLogLevelCfg() {
    // now we can apply log level propagation
    for (const ELogLevelCfg& cfg : m_logLevelCfg) {
        ELOG_REPORT_TRACE("Setting %s log level to %s (propagate - %u)",
                          cfg.m_logSource->getQualifiedName(), elogLevelToStr(cfg.m_logLevel),
                          (uint32_t)cfg.m_propagationMode);
        cfg.m_logSource->setLogLevel(cfg.m_logLevel, cfg.m_propagationMode);
        dbgutil::setLoggerSeverity(cfg.m_dbgUtilLoggerId, cfg.m_severity);
    }
}

}  // namespace elog

#endif  // ELOG_ENABLE_STACK_TRACE
