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
#define ELOG_INVALID_TARGET_ID ((elog::ELogTargetId)0xFFFFFFFF)

class ELogSystem {
public:
    /** @brief Initializes the ELog system with defaults (log to standard error stream). */
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

    /** @brief Releases all resources allocated for the ELogSystem. */
    static void terminate();

    // log targets (API is not thread safe)

    /**
     * @brief Replaces all currently configured log targets with the given log target.
     * @param target The log target.
     * @param printBanner Optionally specifies whether to print a banner (if not specified none is
     * printed).
     * @return ELogTargetId The resulting log target identifier or @ref ELOG_INVALID_TARGET_ID if
     * failed.
     */
    static ELogTargetId setLogTarget(ELogTarget* target, bool printBanner = false);

    /**
     * @brief Utility method for replacing all currently configured log targets with a file log
     * target.
     * @param logFilePath The path to the log file.
     * @param flushPolicy Optional flush policy (if not specified then flush after each log
     * message).
     * @param printBanner Optionally specifies whether to print a banner (by default printed).
     * @return ELogTargetId The resulting log target identifier or @ref ELOG_INVALID_TARGET_ID if
     * failed.
     */
    static ELogTargetId setLogFileTarget(const char* logFilePath,
                                         ELogFlushPolicy* flushPolicy = nullptr,
                                         bool printBanner = true);

    /**
     * @brief Utility method for replacing all currently configured log targets with a file log
     * target.
     * @param fileHandle An open file handle. Standard output or error stream handles can be
     * specified here. The caller is responsible for closing the handle when done if needed.
     * @param flushPolicy Optional flush policy (if not specified then flush after each log
     * message).
     * @param printBanner Optionally specifies whether to print a banner (if not specified none is
     * printed).
     * @return ELogTargetId The resulting log target identifier or @ref ELOG_INVALID_TARGET_ID if
     * failed.
     */
    static ELogTargetId setLogFileTarget(FILE* fileHandle, ELogFlushPolicy* flushPolicy = nullptr,
                                         bool printBanner = false);

    /**
     * @brief Utility method for replacing all currently configured log targets with a segmented
     * file log target.
     * @param logPath The directory for log file segments.
     * @param logName The base name for log file segments.
     * @param flushPolicy Optional flush policy (if not specified then flush after each log
     * message).
     * @param printBanner Optionally specifies whether to print a banner (by default printed).
     * @return ELogTargetId The resulting log target identifier or @ref ELOG_INVALID_TARGET_ID if
     * failed.
     */
    static ELogTargetId setSegmentedLogFileTarget(const char* logPath, const char* logName,
                                                  uint32_t segmentLimitMB,
                                                  ELogFlushPolicy* flushPolicy = nullptr,
                                                  bool printBanner = true);

    /**
     * @brief Adds a log target to existing log targets.
     * @param target The target to add
     * @return ELogTargetId The resulting log target identifier or @ref ELOG_INVALID_TARGET_ID if
     * failed.
     */
    static ELogTargetId addLogTarget(ELogTarget* target);

    /**
     * @brief Utility method for adding a file log target.
     * @param logFilePath The path to the log file.
     * @param flushPolicy Optional flush policy (if not specified then flush after each log
     * message).
     * @return ELogTargetId The resulting log target identifier or @ref ELOG_INVALID_TARGET_ID if
     * failed.
     */
    static ELogTargetId addLogFileTarget(const char* logFilePath,
                                         ELogFlushPolicy* flushPolicy = nullptr);

    /**
     * @brief Utility method for adding a file log target.
     * @param fileHandle An open file handle. Standard output or error stream handles can be
     * specified here. The caller is responsible for closing the handle when done if needed.
     * @param flushPolicy Optional flush policy (if not specified then flush after each log
     * message).
     * @return ELogTargetId The resulting log target identifier or @ref ELOG_INVALID_TARGET_ID if
     * failed.
     */
    static ELogTargetId addLogFileTarget(FILE* fileHandle, ELogFlushPolicy* flushPolicy = nullptr);

    /**
     * @brief Adds a segmented file log target.
     * @param logPath The directory for log file segments.
     * @param logName The base name for log file segments.
     * @param flushPolicy Optional flush policy (if not specified then flush after each log
     * message).
     * @return ELogTargetId The resulting log target identifier or @ref ELOG_INVALID_TARGET_ID if
     * failed.
     */
    static ELogTargetId addSegmentedLogFileTarget(const char* logPath, const char* logName,
                                                  uint32_t segmentLimitMB,
                                                  ELogFlushPolicy* flushPolicy = nullptr);

    /**
     * @brief Removes an existing log target.
     * @param target The log target to remove.
     * @note It is preferable to use to other @ref removeLogTarget() method, as it runs faster.
     */
    static void removeLogTarget(ELogTarget* target);

    /**
     * @brief Removes an existing log target.
     * @param targetId The identifier of the log target to removed as obtained when adding or
     * setting the log target.
     */
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

inline bool isLogLevelEnabled(ELogLevel logLevel) {
    return ELogSystem::getDefaultLogger()->canLog(logLevel);
}

}  // namespace elog

/**
 * @brief Logs a formatted message to the server log.
 * @param logger The logger used for message formatting.
 * @param level The log level. If the log level is insufficient, then the message is dropped.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_EX(logger, level, fmt, ...)              \
    if (logger->canLog(level)) {                      \
        logger->logFormat(level, fmt, ##__VA_ARGS__); \
    }

/**
 * @brief Logs a fatal message to the server log.
 * @param logger The logger used for message formatting.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_FATAL_EX(logger, fmt, ...) ELOG_EX(logger, elog::ELEVEL_FATAL, fmt, ##__VA_ARGS__)

/**
 * @brief Logs an error message to the server log.
 * @param logger The logger used for message formatting.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_ERROR_EX(logger, fmt, ...) ELOG_EX(logger, elog::ELEVEL_ERROR, fmt, ##__VA_ARGS__)

/**
 * @brief Logs a warning message to the server log.
 * @param logger The logger used for message formatting.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_WARN_EX(logger, fmt, ...) ELOG_EX(logger, elog::ELEVEL_WARN, fmt, ##__VA_ARGS__)

/**
 * @brief Logs a notice message to the server log.
 * @param logger The logger used for message formatting.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_NOTICE_EX(logger, fmt, ...) ELOG_EX(logger, elog::ELEVEL_NOTICE, fmt, ##__VA_ARGS__)

/**
 * @brief Logs an informational message to the server log.
 * @param logger The logger used for message formatting.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_INFO_EX(logger, fmt, ...) ELOG_EX(logger, elog::ELEVEL_INFO, fmt, ##__VA_ARGS__)

/**
 * @brief Logs a trace message to the server log.
 * @param logger The logger used for message formatting.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_TRACE_EX(logger, fmt, ...) ELOG_EX(logger, elog::ELEVEL_TRACE, fmt, ##__VA_ARGS__)

/**
 * @brief Logs a debug message to the server log.
 * @param logger The logger used for message formatting.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_DEBUG_EX(logger, fmt, ...) ELOG_EX(logger, elog::ELEVEL_DEBUG, fmt, ##__VA_ARGS__)

/**
 * @brief Logs a diagnostic message to the server log.
 * @param logger The logger used for message formatting.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_DIAG_EX(logger, fmt, ...) ELOG_EX(logger, elog::ELEVEL_DIAG, fmt, ##__VA_ARGS__)

/**
 * @brief Begins a multi-part log message.
 * @param logger The logger used for message formatting.
 * @param level The log level. If the log level is insufficient, then the message is dropped.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_BEGIN_EX(logger, level, fmt, ...)       \
    if (logger->canLog(level)) {                     \
        logger->startLog(level, fmt, ##__VA_ARGS__); \
    }

/**
 * @brief Appends formatted message to a multi-part log message.
 * @param logger The logger used for message formatting.
 * @param fmt The message format.
 * @param ... The message arguments.
 */
#define ELOG_APPEND_EX(logger, fmt, ...) logger->appendLog(fmt, ##__VA_ARGS__)

/**
 * @brief Appends unformatted message to a multi-part log message.
 * @param logger The logger used for message formatting.
 * @param msg The log message.
 */
#define ELOG_APPEND_NF_EX(logger, msg) logger->appendLogNoFormat(msg)

/**
 * @brief Terminates a multi-part log message and writes it to the server log.
 * @param logger The logger used for message formatting.
 */
#define ELOG_END_EX(logger) logger->finishLog()

/**
 * @brief Logs a system error message to the server log.
 * @param logger The logger used for message formatting.
 * @param syscall The system call that failed.
 * @param sysErr The system error code.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_SYS_ERROR_NUM_EX(logger, syscall, sysErr, fmt, ...)                    \
    {                                                                               \
        ELOG_ERROR_EX(logger, "System call " #syscall "() failed: %d (%s)", sysErr, \
                      elog::ELogSystem::sysErrorToStr(sysErr));                     \
        ELOG_ERROR_EX(logger, fmt, ##__VA_ARGS__);                                  \
    }

/**
 * @brief Logs a system error message to the server log.
 * @param logger The logger used for message formatting.
 * @param syscall The system call that failed.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 * @note The error code for the system call is obtained through @ref errno. If you wish to provide
 * another error code then consider calling @ref ELOG_SYS_ERROR_NUM() instead.
 */
#define ELOG_SYS_ERROR_EX(logger, syscall, fmt, ...) \
    ELOG_SYS_ERROR_NUM_EX(logger, syscall, errno, fmt, ##__VA_ARGS__)

#ifdef __WIN32__
/**
 * @brief Logs a system error message to the server log.
 * @param logger The logger used for message formatting.
 * @param syscall The system call that failed.
 * @param sysErr The system error code.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_WIN32_ERROR_NUM_EX(logger, syscall, sysErr, fmt, ...)                          \
    {                                                                                       \
        char* errStr = elog::ELogSystem::win32SysErrorToStr(sysErr);                        \
        ELOG_ERROR_EX(logger, "Windows system call " #syscall "() failed: %d (%s)", sysErr, \
                      errStr);                                                              \
        elog::ELogSystem::win32FreeErrorStr(errStr);                                        \
        ELOG_ERROR_EX(logger, fmt, ##__VA_ARGS__);                                          \
    }

/**
 * @brief Logs a system error message to the server log.
 * @param logger The logger used for message formatting.
 * @param syscall The system call that failed.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 * @note The error code for the system call is obtained through @ref errno. If you wish to provide
 * another error code then consider calling @ref ELOG_SYS_ERROR_NUM() instead.
 */
#define ELOG_WIN32_ERROR_EX(logger, syscall, fmt, ...) \
    ELOG_WIN32_ERROR_NUM_EX(logger, syscall, ::GetLastError(), fmt, ##__VA_ARGS__)

#endif  // __WIN32__

/**
 * @brief Logs a formatted message to the server log (using default logger).
 * @param level The log level. If the log level is insufficient, then the message is dropped.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG(level, fmt, ...)                                            \
    {                                                                    \
        elog::ELogLogger* logger = elog::ELogSystem::getDefaultLogger(); \
        ELOG_EX(logger, level, fmt, ##__VA_ARGS__);                      \
    }

/**
 * @brief Logs a fatal message to the server log.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_FATAL(fmt, ...) ELOG(elog::ELEVEL_FATAL, fmt, ##__VA_ARGS__)

/**
 * @brief Logs an error message to the server log.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_ERROR(fmt, ...) ELOG(elog::ELEVEL_ERROR, fmt, ##__VA_ARGS__)

/**
 * @brief Logs a warning message to the server log.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_WARN(fmt, ...) ELOG(elog::ELEVEL_WARN, fmt, ##__VA_ARGS__)

/**
 * @brief Logs a notice message to the server log.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_NOTICE(fmt, ...) ELOG(elog::ELEVEL_NOTICE, fmt, ##__VA_ARGS__)

/**
 * @brief Logs an informational message to the server log.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_INFO(fmt, ...) ELOG(elog::ELEVEL_INFO, fmt, ##__VA_ARGS__)

/**
 * @brief Logs a trace message to the server log.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_TRACE(fmt, ...) ELOG(elog::ELEVEL_TRACE, fmt, ##__VA_ARGS__)

/**
 * @brief Logs a debug message to the server log.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_DEBUG(fmt, ...) ELOG(elog::ELEVEL_DEBUG, fmt, ##__VA_ARGS__)

/**
 * @brief Logs a diagnostic message to the server log.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_DIAG(fmt, ...) ELOG(elog::ELEVEL_DIAG, fmt, ##__VA_ARGS__)

/**
 * @brief Begins a multi-part log message.
 * @param level The log level. If the log level is insufficient, then the message is dropped.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_BEGIN(level, fmt, ...)                                      \
    {                                                                    \
        elog::ELogLogger* logger = elog::ELogSystem::getDefaultLogger(); \
        ELOG_BEGIN_EX(logger, level, fmt, ##__VA_ARGS__);                \
    }

/**
 * @brief Appends formatted message to a multi-part log message.
 * @param fmt The message format.
 * @param ... The message arguments.
 */
#define ELOG_APPEND(fmt, ...) \
    ELOG_APPEND_EX(elog::ELogSystem::getDefaultLogger(), fmt, ##__VA_ARGS__);

/**
 * @brief Appends unformatted message to a multi-part log message.
 * @param msg The log message.
 */
#define ELOG_APPEND_NF(msg) ELOG_APPEND_NF_EX(elog::ELogSystem::getDefaultLogger(), msg)

/**
 * @brief Terminates a multi-part log message and writes it to the server log.
 */
#define ELOG_END() ELOG_END_EX(elog::ELogSystem::getDefaultLogger())

/**
 * @brief Logs a system error message to the server log.
 * @param syscall The system call that failed.
 * @param sysErr The system error code.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_SYS_ERROR_NUM(syscall, sysErr, fmt, ...)                       \
    {                                                                       \
        elog::ELogLogger* logger = elog::ELogSystem::getDefaultLogger();    \
        ELOG_SYS_ERROR_NUM_EX(logger, syscall, sysErr, fmt, ##__VA_ARGS__); \
    }

/**
 * @brief Logs a system error message to the server log.
 * @param syscall The system call that failed.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 * @note The error code for the system call is obtained through @ref errno. If you wish to provide
 * another error code then consider calling @ref ELOG_SYS_ERROR_NUM() instead.
 */
#define ELOG_SYS_ERROR(syscall, fmt, ...) ELOG_SYS_ERROR_NUM(syscall, errno, fmt, ##__VA_ARGS__)

#ifdef __WIN32__
/**
 * @brief Logs a system error message to the server log.
 * @param syscall The system call that failed.
 * @param sysErr The system error code.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_WIN32_ERROR_NUM(syscall, sysErr, fmt, ...)                       \
    {                                                                         \
        elog::ELogLogger* logger = elog::ELogSystem::getDefaultLogger();      \
        ELOG_WIN32_ERROR_NUM_EX(logger, syscall, sysErr, fmt, ##__VA_ARGS__); \
    }

/**
 * @brief Logs a system error message to the server log.
 * @param syscall The system call that failed.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 * @note The error code for the system call is obtained through @ref errno. If you wish to provide
 * another error code then consider calling @ref ELOG_SYS_ERROR_NUM() instead.
 */
#define ELOG_WIN32_ERROR(syscall, fmt, ...) \
    ELOG_WIN32_ERROR_NUM(syscall, ::GetLastError(), fmt, ##__VA_ARGS__)

#endif  // __WIN32__

#define ELOG_USING()           \
    using elog::ELEVEL_DEBUG;  \
    using elog::ELEVEL_DIAG;   \
    using elog::ELEVEL_ERROR;  \
    using elog::ELEVEL_FATAL;  \
    using elog::ELEVEL_INFO;   \
    using elog::ELEVEL_NOTICE; \
    using elog::ELEVEL_TRACE;  \
    using elog::ELEVEL_WARN;   \
    using elog::ELogLevel;

#if 0
/**
 * @brief Logs libuv error message to the server log.
 * @param logger The logger used for message formatting.
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
#endif

#endif  // __ELOG_SYSTEM_H__