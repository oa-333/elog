#include "elog_dbg_util_log_handler.h"

#ifdef ELOG_ENABLE_STACK_TRACE

#include <algorithm>
#include <string>

#include "elog_config_parser.h"
#include "elog_error.h"
#include "elog_system.h"

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
                                                             uint32_t loggerId) {
    // define a log source
    std::string qualifiedLoggerName = std::string("dbgutil.") + loggerName;
    ELogSource* logSource = ELogSystem::defineLogSource(qualifiedLoggerName.c_str(), true);
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
    char* envVarValue = getenv(envVarName.c_str());
    if (envVarValue != nullptr) {
        ELogLevel logLevel = ELEVEL_INFO;
        ELogPropagateMode propagateMode = ELogPropagateMode::PM_NONE;
        if (!ELogConfigParser::parseLogLevel(envVarValue, logLevel, propagateMode)) {
            ELOG_REPORT_ERROR("Invalid dbgutil source %s log level: %s",
                              qualifiedLoggerName.c_str(), envVarValue);
        } else {
            // we first set logger severity (in the end we will deal with propagation)
            ELOG_REPORT_TRACE("Setting %s initial log level to %s (no propagation)",
                              logSource->getQualifiedName(), elogLevelToStr(logLevel),
                              (uint32_t)propagateMode);
            logSource->setLogLevel(logLevel, propagateMode);
            dbgutil::setLoggerSeverity(loggerId, severity);
            m_logLevelCfg.push_back({logSource, logLevel, propagateMode, loggerId, severity});
        }

        severity = logLevelToSeverity(logLevel);
    }

    return severity;
}

void ELogDbgUtilLogHandler::onUnregisterLogger(uint32_t loggerId) {
    if (loggerId < m_dbgUtilLoggers.size()) {
        m_dbgUtilLoggers[loggerId] = nullptr;
        uint32_t maxLoggerId = 0;
        for (int i = m_dbgUtilLoggers.size() - 1; i >= 0; --i) {
            if (m_dbgUtilLoggers[i] != nullptr) {
                maxLoggerId = i;
                break;
            }
        }
        if (maxLoggerId + 1 < m_dbgUtilLoggers.size()) {
            m_dbgUtilLoggers.resize(maxLoggerId + 1);
        }
    }
}

void ELogDbgUtilLogHandler::onMsg(dbgutil::LogSeverity severity, uint32_t loggerId,
                                  const char* loggerName, const char* msg) {
    // locate logger by id
    ELogLogger* logger = nullptr;
    if (loggerId < m_dbgUtilLoggers.size()) {
        logger = m_dbgUtilLoggers[loggerId];
    }
    if (logger != nullptr) {
        ELogLevel logLevel = severityToLogLevel(severity);
        if (logger->canLog(logLevel)) {
            logger->logFormat(logLevel, "", 0, "", msg);
        } else {
            ELOG_TRACE("Discarded dbgutil log source %s message %s, severity %u",
                       logger->getLogSource()->getQualifiedName(), msg, (unsigned)severity);
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
