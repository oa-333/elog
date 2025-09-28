#ifndef __ELOG_COMM_UTIL_LOG_HANDLER_H__
#define __ELOG_COMM_UTIL_LOG_HANDLER_H__

#ifdef ELOG_USING_COMM_UTIL

#include <vector>

#include "comm_util.h"
#include "elog_level_cfg.h"
#include "elog_logger.h"
#include "elog_report.h"

namespace elog {

class ELogCommUtilLogHandler : public commutil::LogHandler {
public:
    ELogCommUtilLogHandler() {}
    ELogCommUtilLogHandler(const ELogCommUtilLogHandler&) = delete;
    ELogCommUtilLogHandler(ELogCommUtilLogHandler&&) = delete;
    ELogCommUtilLogHandler& operator=(ELogCommUtilLogHandler&) = delete;
    ~ELogCommUtilLogHandler() final {}

    /**
     * @brief Notifies that a logger has been registered.
     * @param severity The log severity with which the logger was initialized.
     * @param loggerName The name of the logger that was registered.
     * @param loggerId The identifier used to refer to this logger.
     * @return LogSeverity The desired severity for the logger. If not to be changed, then
     * return the severity with which the logger was registered.
     */
    commutil::LogSeverity onRegisterLogger(commutil::LogSeverity severity, const char* loggerName,
                                           size_t loggerId) final;

    /** @brief Unregisters a previously registered logger. */
    void onUnregisterLogger(size_t loggerId) final;

    /**
     * @brief Notifies a logger is logging a message.
     * @param severity The log message severity.
     * @param loggerId The identifier used to refer to this logger.
     * @param msg The log message.
     */
    void onMsg(commutil::LogSeverity severity, size_t loggerId, const char* loggerName,
               const char* msg) final;

    /**
     * @brief Allows the handler to set current thread name.
     * @param threadName The new thread name.
     */
    void onThreadStart(const char* threadName) final;

    /**
     * @brief Applies delayed log level configuration (including propagation) for different
     * loggers.
     */
    void applyLogLevelCfg();

    /** @brief Refreshes log level of all registered loggers. */
    void refreshLogLevelCfg();

private:
    std::vector<ELogCommLevelCfg> m_logLevelCfg;
    std::vector<ELogReportLogger*> m_commUtilLoggers;
};

}  // namespace elog

#endif  // ELOG_USING_COMM_UTIL

#endif  // __ELOG_COMM_UTIL_LOG_HANDLER_H__