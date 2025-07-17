#ifndef __ELOG_DBG_UTIL_LOG_HANDLER_H__
#define __ELOG_DBG_UTIL_LOG_HANDLER_H__

#ifdef ELOG_ENABLE_STACK_TRACE

#include <vector>

#include "dbg_util.h"
#include "elog_level_cfg.h"
#include "elog_logger.h"

namespace elog {

class ELogDbgUtilLogHandler : public dbgutil::LogHandler {
public:
    ELogDbgUtilLogHandler() {}
    ELogDbgUtilLogHandler(const ELogDbgUtilLogHandler&) = delete;
    ELogDbgUtilLogHandler(ELogDbgUtilLogHandler&&) = delete;
    ELogDbgUtilLogHandler& operator=(ELogDbgUtilLogHandler&) = delete;
    ~ELogDbgUtilLogHandler() final {}

    /**
     * @brief Notifies that a logger has been registered.
     * @param severity The log severity with which the logger was initialized.
     * @param loggerName The name of the logger that was registered.
     * @param loggerId The identifier used to refer to this logger.
     * @return LogSeverity The desired severity for the logger. If not to be changed, then
     * return the severity with which the logger was registered.
     */
    dbgutil::LogSeverity onRegisterLogger(dbgutil::LogSeverity severity, const char* loggerName,
                                          size_t loggerId) final;

    /** @brief Unregisters a previously registered logger. */
    void onUnregisterLogger(size_t loggerId) final;

    /**
     * @brief Notifies a logger is logging a message.
     * @param severity The log message severity.
     * @param loggerId The identifier used to refer to this logger.
     * @param msg The log message.
     */
    void onMsg(dbgutil::LogSeverity severity, size_t loggerId, const char* loggerName,
               const char* msg) final;

    /**
     * @brief Applies delayed log level configuration (including propagation) for different
     * loggers.
     */
    void applyLogLevelCfg();

private:
    std::vector<ELogLevelCfg> m_logLevelCfg;
    std::vector<ELogLogger*> m_dbgUtilLoggers;
};

}  // namespace elog

#endif  // ELOG_ENABLE_STACK_TRACE

#endif  // __ELOG_DBG_UTIL_LOG_HANDLER_H__