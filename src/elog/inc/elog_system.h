#ifndef __ELOG_SYSTEM_H__
#define __ELOG_SYSTEM_H__

#include <cstdarg>
#include <cstdio>

#include "elog_filter.h"
#include "elog_formatter.h"
#include "elog_level.h"
#include "elog_logger.h"
#include "elog_source.h"
#include "elog_target.h"

namespace elog {

typedef uint32_t ELogTargetId;
#define ELOG_INVALID_TARGET_ID ((ELogTargetId)0xFFFFFFFF)

class ELogSystem {
public:
    /** @brief Initializes the ELog system with defaults. */
    static bool initialize();

    /**
     * @brief Initializes the ELog system for logging to a log file.
     * @param logFilePath The log file path (including lgo file name).
     * @param flushPolicy Optional flush policy. If not specified default is used (which
     * flushes after event log message).
     * @param logFilter Optional log filter. If none specified, all messages are written to
     * log.
     * @param logFormatter Optional log formatter. If none specified default log line format is
     * used.
     * @return true If succeeded, otherwise false.
     */
    static bool initializeLogFile(const char* logFilePath, ELogFlushPolicy* flushPolicy = nullptr,
                                  ELogFilter* logFilter = nullptr,
                                  ELogFormatter* logFormatter = nullptr);

    /**
     * @brief Initializes the ELog system for logging to a segmented log file.
     * @param logPath The log path (path to directory for log segments).
     * @param logName The base segment name (including file extension if desired).
     * @param segmentLimitMB The size limit of each log segment.
     * @param flushPolicy Optional flush policy. If not specified default is used (which
     * flushes after event log message).
     * @param logFilter Optional log filter. If none specified, all messages are written to
     * log.
     * @param logFormatter Optional log formatter. If none specified default log line format is
     * used.
     * @return true If succeeded, otherwise false.
     */
    static bool initializeSegmentedLogFile(const char* logPath, const char* logName,
                                           uint32_t segmentLimitMB,
                                           ELogFlushPolicy* flushPolicy = nullptr,
                                           ELogFilter* logFilter = nullptr,
                                           ELogFormatter* logFormatter = nullptr);

    // log targets (API is not thread safe)
    static ELogTargetId setLogTarget(ELogTarget* target, bool printBanner = false);
    static ELogTargetId setFileLogTarget(const char* logFilePath, bool printBanner = true);
    static ELogTargetId setFileLogTarget(FILE* fileHandle, bool printBanner = false);
    static ELogTargetId addLogTarget(ELogTarget* target);
    static ELogTargetId addFileLogTarget(const char* logFilePath);
    static ELogTargetId addFileLogTarget(FILE* fileHandle);
    static void removeLogTarget(ELogTarget* target);
    static void removeLogTarget(ELogTargetId targetId);

    // log sources
    static ELogSourceId defineLogSource(const char* qualifiedName);
    static ELogSource* getLogSource(const char* qualifiedName);
    static ELogSource* getLogSource(ELogSourceId logSourceId);
    static ELogSource* getRootLogSource();
    // static void configureLogSourceLevel(const char* qualifiedName, ELogLevel logLevel);

    // logger interface
    static ELogLogger* getDefaultLogger();
    static ELogLogger* getLogger(const char* sourceName);
    // static ELogLogger* getMultiThreadedLogger(const char* sourceName);
    // static ELogLogger* getSingleThreadedLogger(const char* sourceName);

    // log formatting
    static bool configureLogFormat(const char* logFormat);
    static void setLogFormatter(ELogFormatter* logFormatter);
    // static void setLogFormatter(ELogFormatter* formatter);
    static void formatLogMsg(const ELogRecord& logRecord, std::string& logMsg);

    // global log filtering
    static void setLogFilter(ELogFilter* logFilter);
    static bool filterLogMsg(const ELogRecord& logRecord);

    // note: each log target controls its own log format

    static void log(const ELogRecord& logRecord);

    /** @brief Converts system error code to string. */
    static char* sysErrorToStr(int sysErrorCode);

#ifdef __WIN32__
    static char* win32SysErrorToStr(unsigned long sysErrorCode);
    static void win32FreeErrorStr(char* errStr);
#endif

private:
    static bool initGlobals();
    static void termGlobals();
};

}  // namespace elog

/**
 * @brief Logs a formatted message to the server log.
 * @param level The log level. If the log level is insufficient, then the message is dropped.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG(logger, level, fmt, ...)                 \
    if (logger->canLog(level)) {                      \
        logger->logFormat(level, fmt, ##__VA_ARGS__); \
    }

/**
 * @brief Logs a fatal message to the server log.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_FATAL(logger, fmt, ...) ELOG(logger, elog::ELOG_FATAL, fmt, ##__VA_ARGS__)

/**
 * @brief Logs an error message to the server log.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_ERROR(logger, fmt, ...) ELOG(logger, elog::ELOG_ERROR, fmt, ##__VA_ARGS__)

/**
 * @brief Logs a warning message to the server log.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_WARN(logger, fmt, ...) ELOG(logger, elog::ELOG_WARN, fmt, ##__VA_ARGS__)

/**
 * @brief Logs a notice message to the server log.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_NOTICE(logger, fmt, ...) ELOG(logger, elog::ELOG_NOTICE, fmt, ##__VA_ARGS__)

/**
 * @brief Logs an informational message to the server log.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_INFO(logger, fmt, ...) ELOG(logger, elog::ELOG_INFO, fmt, ##__VA_ARGS__)

/**
 * @brief Logs a trace message to the server log.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_TRACE(logger, fmt, ...) ELOG(logger, elog::ELOG_TRACE, fmt, ##__VA_ARGS__)

/**
 * @brief Logs a debug message to the server log.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_DEBUG(logger, fmt, ...) ELOG(logger, elog::ELOG_DEBUG, fmt, ##__VA_ARGS__)

/**
 * @brief Begins a multi-part log message.
 * @param level The log level. If the log level is insufficient, then the message is dropped.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_BEGIN(logger, level, fmt, ...)          \
    if (logger->canLog(level)) {                     \
        logger->startLog(level, fmt, ##__VA_ARGS__); \
    }

/**
 * @brief Appends formatted message to a multi-part log message.
 * @param fmt The message format.
 * @param ... The message arguments.
 */
#define ELOG_APPEND(logger, level, fmt, ...)   \
    if (logger->canLog(level)) {               \
        logger->appendLog(fmt, ##__VA_ARGS__); \
    }

/**
 * @brief Appends unformatted message to a multi-part log message.
 * @param msg The log message.
 */
#define ELOG_APPEND_NF(logger, level, msg) \
    if (logger->canLog(level)) {           \
        logger->appendLogNoFormat(msg);    \
    }

/** @brief Terminates a multi-part log message and writes it to the server log. */
#define ELOG_END(logger, level)  \
    if (logger->canLog(level)) { \
        logger->finishLog(msg);  \
    }

/**
 * @brief Logs a system error message to the server log.
 * @param syscall The system call that failed.
 * @param sysErr The system error code.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_SYS_ERROR_NUM(logger, syscall, sysErr, fmt, ...)                    \
    {                                                                            \
        ELOG_ERROR(logger, "System call " #syscall "() failed: %d (%s)", sysErr, \
                   elog::ELogSystem::sysErrorToStr(sysErr));                     \
        ELOG_ERROR(logger, fmt, ##__VA_ARGS__);                                  \
    }

/**
 * @brief Logs a system error message to the server log.
 * @param syscall The system call that failed.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 * @note The error code for the system call is obtained through @ref errno. If you wish to provide
 * another error code then consider calling @ref ELOG_SYS_ERROR_NUM() instead.
 */
#define ELOG_SYS_ERROR(logger, syscall, fmt, ...) \
    ELOG_SYS_ERROR_NUM(logger, syscall, errno, fmt, ##__VA_ARGS__)

#ifdef __WIN32__
/**
 * @brief Logs a system error message to the server log.
 * @param syscall The system call that failed.
 * @param sysErr The system error code.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_WIN32_ERROR_NUM(logger, syscall, sysErr, fmt, ...)                                   \
    {                                                                                             \
        char* errStr = elog::ELogSystem::win32SysErrorToStr(sysErr);                              \
        ELOG_ERROR(logger, "Windows system call " #syscall "() failed: %d (%s)", sysErr, errStr); \
        elog::ELogSystem::win32FreeErrorStr(errStr);                                              \
        ELOG_ERROR(logger, fmt, ##__VA_ARGS__);                                                   \
    }

/**
 * @brief Logs a system error message to the server log.
 * @param syscall The system call that failed.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 * @note The error code for the system call is obtained through @ref errno. If you wish to provide
 * another error code then consider calling @ref ELOG_SYS_ERROR_NUM() instead.
 */
#define ELOG_WIN32_ERROR(logger, syscall, fmt, ...) \
    ELOG_WIN32_ERROR_NUM(logger, syscall, ::GetLastError(), fmt, ##__VA_ARGS__)

#endif  // __WIN32__

/**
 * @brief Logs libuv error message to the server log.
 * @param uvcall The libuv call that failed.
 * @param res The libuv call result.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_UV_ERROR(logger, uvcall, res, fmt, ...)                        \
    {                                                                       \
        char buf[32];                                                       \
        ELOG_ERROR(logger, "libuv call " #uvcall "() failed: %d (%s)", res, \
                   uv_strerror_r(res, buf, 32));                            \
        ELOG_ERROR(logger, fmt, ##__VA_ARGS__);                             \
    }

#endif  // __ELOG_SYSTEM_H__