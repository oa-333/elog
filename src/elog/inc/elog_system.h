#ifndef __ELOG_SYSTEM_H__
#define __ELOG_SYSTEM_H__

#include "elog_config.h"
#include "elog_error_handler.h"
#include "elog_filter.h"
#include "elog_formatter.h"
#include "elog_level.h"
#include "elog_logger.h"
#include "elog_props.h"
#include "elog_schema_handler.h"
#include "elog_source.h"
#include "elog_target.h"

#ifdef ELOG_ENABLE_STACK_TRACE
#include "dbg_stack_trace.h"
#endif

namespace elog {

/** @brief The elog module facade. */
class ELOG_API ELogSystem {
public:
    /**
     * @brief Initializes the ELog system with defaults (log to standard error stream).
     * @param errorHandler Optional error handler. If none specified, then all internal errors are
     * sent to the standard output stream.
     * @return true If succeeded, otherwise false.
     */
    static bool initialize(ELogErrorHandler* errorHandler = nullptr);

    /**
     * @brief Initializes the ELog system for logging to a log file.
     * @param logFilePath The log file path (including lgo file name).
     * @param bufferSize Optional buffer size for file buffering. Specify zero to disable buffering.
     * @param useLock (Relevant only for buffered logging) Optionally specify use of lock. By
     * default none is used. Pay attention that when buffering is used in a multi-threaded scenario,
     * using a lock is mandatory, and without a lock behavior is undefined.
     * @param errorHandler Optional error handler. If none specified, then all internal errors are
     * sent to the standard output stream.
     * @param flushPolicy Optional flush policy. If not specified default is used (which
     * flushes after event log message).
     * @param logFilter Optional log filter. If none specified, all messages are written to
     * log.
     * @param logFormatter Optional log formatter. If none specified default log line format is
     * used.
     * @return true If succeeded, otherwise false.
     */
    static bool initializeLogFile(const char* logFilePath, uint32_t bufferSize = 0,
                                  bool useLock = false, ELogErrorHandler* errorHandler = nullptr,
                                  ELogFlushPolicy* flushPolicy = nullptr,
                                  ELogFilter* logFilter = nullptr,
                                  ELogFormatter* logFormatter = nullptr);

    /**
     * @brief Initializes the ELog system for logging to a segmented log file.
     * @param errorHandler Optional error handler. If none specified, then all internal errors are
     * sent to the standard output stream.
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
                                           ELogErrorHandler* errorHandler = nullptr,
                                           ELogFlushPolicy* flushPolicy = nullptr,
                                           ELogFilter* logFilter = nullptr,
                                           ELogFormatter* logFormatter = nullptr);

    /** @brief Releases all resources allocated for the ELogSystem. */
    static void terminate();

    /** @brief Installs an error handler. */
    static void setErrorHandler(ELogErrorHandler* errorHandler);

    /** @brief Configures elog tracing. */
    static void setTraceMode(bool enableTrace = true);

    /** @brief Queries whether trace mode is enabled. */
    static bool isTraceEnabled();

    /** @brief Registers a schema handler by name. */
    static bool registerSchemaHandler(const char* schemeName, ELogSchemaHandler* schemaHandler);

    /**
     * @brief Configures the ELog System from a properties configuration file.
     * The following properties are recognized:
     * - log_format: log line format specification. See @ref configureLogFormat() for more details.
     * - log_level: expected value is any log level string (without the "ELEVEL_" prefix).
     *   Determines the global (root source) log level.
     * - <qualified-source-name>.log_level: Log level of a log source.
     * - log_target: expected log target URL.
     * @param props The properties map.
     * @param defineLogSources[opt] Optional parameter specifying whether each log source
     * configuration item triggers creation of the log source.
     * @param defineMissingPath[opt] In case @ref defineLogSources is true, then optionally define
     * all missing loggers along the name path. If not specified, and a logger on the path from root
     * to leaf is missing, then the call fails.
     * @return true If configuration succeeded, otherwise false.
     */
    static bool configureFromFile(const char* configPath, bool defineLogSources = false,
                                  bool defineMissingPath = false);

    /**
     * @brief Configures the ELog System from a properties map.
     * The following properties are recognized:
     * - log_format: log line format specification. See @ref configureLogFormat() for more details.
     * - log_level: expected value is any log level string (without the "ELEVEL_" prefix).
     *   Determines the global (root source) log level.
     * - <qualified-source-name>.log_level: Log level of a log source.
     * - log_target: expected log target URL.
     * @param props The properties map.
     * @param defineLogSources[opt] Optional parameter specifying whether each log source
     * configuration item triggers creation of the log source.
     * @param defineMissingPath[opt] In case @ref defineLogSources is true, then optionally define
     * all missing loggers along the name path. If not specified, and a logger on the path from root
     * to leaf is missing, then the call fails.
     * @return true If configuration succeeded, otherwise false.
     */
    static bool configureFromProperties(const ELogPropertySequence& props,
                                        bool defineLogSources = false,
                                        bool defineMissingPath = false);

    /**
     * @brief Configures the ELog System from a properties configuration file (extended
     * functionality, loading through unified configuration interface, allowing for source location
     * information).
     * The following properties are recognized:
     * - log_format: log line format specification. See @ref configureLogFormat() for more details.
     * - log_level: expected value is any log level string (without the "ELEVEL_" prefix).
     *   Determines the global (root source) log level.
     * - <qualified-source-name>.log_level: Log level of a log source.
     * - log_target: expected log target URL.
     * @param props The properties map.
     * @param defineLogSources[opt] Optional parameter specifying whether each log source
     * configuration item triggers creation of the log source.
     * @param defineMissingPath[opt] In case @ref defineLogSources is true, then optionally define
     * all missing loggers along the name path. If not specified, and a logger on the path from root
     * to leaf is missing, then the call fails.
     * @return true If configuration succeeded, otherwise false.
     */
    static bool configureFromFileEx(const char* configPath, bool defineLogSources = false,
                                    bool defineMissingPath = false);

    /**
     * @brief Configures the ELog System from a properties map (extended functionality, loading
     * through unified configuration interface).
     * The following properties are recognized:
     * - log_format: log line format specification. See @ref configureLogFormat() for more details.
     * - log_level: expected value is any log level string (without the "ELEVEL_" prefix).
     *   Determines the global (root source) log level.
     * - <qualified-source-name>.log_level: Log level of a log source.
     * - log_target: expected log target URL.
     * @param props The properties map.
     * @param defineLogSources[opt] Optional parameter specifying whether each log source
     * configuration item triggers creation of the log source.
     * @param defineMissingPath[opt] In case @ref defineLogSources is true, then optionally define
     * all missing loggers along the name path. If not specified, and a logger on the path from root
     * to leaf is missing, then the call fails.
     * @return true If configuration succeeded, otherwise false.
     */
    static bool configureFromPropertiesEx(const ELogPropertyPosSequence& props,
                                          bool defineLogSources = false,
                                          bool defineMissingPath = false);

    /**
     * @brief Configures the ELog System from a configuration file (see README for format).
     * The following properties are recognized:
     * - log_format: log line format specification. See @ref configureLogFormat() for more details.
     * - log_level: expected value is any log level string (without the "ELEVEL_" prefix).
     * - log_filter: global log filter (including rate limiter).
     *   Determines the global (root source) log level.
     * - <qualified-source-name>.log_level: Log level of a log source.
     * - log_target: expected log target URL.
     * @note The top level configuration item should be a map.
     * @param configPath The configuration file path.
     * @param defineLogSources[opt] Optional parameter specifying whether each log source
     * configuration item triggers creation of the log source.
     * @param defineMissingPath[opt] In case @ref defineLogSources is true, then optionally define
     * all missing loggers along the name path. If not specified, and a logger on the path from root
     * to leaf is missing, then the call fails.
     * @return true If configuration succeeded, otherwise false.
     */
    static bool configureFromConfigFile(const char* configPath, bool defineLogSources = false,
                                        bool defineMissingPath = false);

    /**
     * @brief Configures the ELog System from a configuration string (see README for format).
     * The following properties are recognized:
     * - log_format: log line format specification. See @ref configureLogFormat() for more details.
     * - log_level: expected value is any log level string (without the "ELEVEL_" prefix).
     * - log_filter: global log filter (including rate limiter).
     *   Determines the global (root source) log level.
     * - <qualified-source-name>.log_level: Log level of a log source.
     * - log_target: expected log target URL.
     * @param configStr The configuration string.
     * @param defineLogSources[opt] Optional parameter specifying whether each log source
     * configuration item triggers creation of the log source.
     * @param defineMissingPath[opt] In case @ref defineLogSources is true, then optionally define
     * all missing loggers along the name path. If not specified, and a logger on the path from root
     * to leaf is missing, then the call fails.
     * @return true If configuration succeeded, otherwise false.
     */
    static bool configureFromConfigStr(const char* configStr, bool defineLogSources = false,
                                       bool defineMissingPath = false);

    /**
     * @brief Configures the ELog System from a configuration object.
     * The following properties are recognized:
     * - log_format: log line format specification. See @ref configureLogFormat() for more details.
     * - log_level: expected value is any log level string (without the "ELEVEL_" prefix).
     * - log_filter: global log filter (including rate limiter).
     *   Determines the global (root source) log level.
     * - <qualified-source-name>.log_level: Log level of a log source.
     * - log_target: expected log target URL.
     * @param config The configuration object. Root node must be of map type (see @ref ELogConfig).
     * @param defineLogSources[opt] Optional parameter specifying whether each log source
     * configuration item triggers creation of the log source.
     * @param defineMissingPath[opt] In case @ref defineLogSources is true, then optionally define
     * all missing loggers along the name path. If not specified, and a logger on the path from root
     * to leaf is missing, then the call fails.
     * @return true If configuration succeeded, otherwise false.
     */
    static bool configure(ELogConfig* config, bool defineLogSources = false,
                          bool defineMissingPath = false);

    /**
     * Log Target Management Interface
     */

    /**
     * @brief Replaces all currently configured log targets with the given log target.
     * @note This API call is not thread-safe, and is recommended to take place during application
     * initialization phase.
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
     * @note This API call is not thread-safe, and is recommended to take place during application
     * initialization phase.
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
     * @note This API call is not thread-safe, and is recommended to take place during application
     * initialization phase.
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
     * @brief Utility method for replacing all currently configured log targets with a buffered file
     * log target.
     * @note This API call is not thread-safe, and is recommended to take place during application
     * initialization phase.
     * @param logFilePath The path to the log file.
     * @param bufferSize Buffer size for file buffering. Cannot be zero.
     * @param useLock Optionally disable use of lock. By default a lock is used because the buffered
     * file log target is not thread-safe. If lock is disabled, and caller does not take care of
     * thread-safety then behavior is undefined.
     * @param flushPolicy Optional flush policy (if not specified then flush after each log
     * message).
     * @param printBanner Optionally specifies whether to print a banner (by default printed).
     * @return ELogTargetId The resulting log target identifier or @ref ELOG_INVALID_TARGET_ID if
     * failed.
     */
    static ELogTargetId setBufferedLogFileTarget(const char* logFilePath, uint32_t bufferSize,
                                                 bool useLock = true,
                                                 ELogFlushPolicy* flushPolicy = nullptr,
                                                 bool printBanner = true);

    /**
     * @brief Utility method for replacing all currently configured log targets with a buffered file
     * log target.
     * @note This API call is not thread-safe, and is recommended to take place during application
     * initialization phase.
     * @param fileHandle An open file handle. Standard output or error stream handles can be
     * specified here. The caller is responsible for closing the handle when done if needed.
     * @param bufferSize Buffer size for file buffering. Cannot be zero.
     * @param useLock Optionally disable use of lock. By default a lock is used because the buffered
     * file log target is not thread-safe. If lock is disabled, and caller does not take care of
     * thread-safety then behavior is undefined.
     * @param flushPolicy Optional flush policy (if not specified then flush after each log
     * message).
     * @param printBanner Optionally specifies whether to print a banner (by default printed).
     * @return ELogTargetId The resulting log target identifier or @ref ELOG_INVALID_TARGET_ID if
     * failed.
     */
    static ELogTargetId setBufferedLogFileTarget(FILE* fileHandle, uint32_t bufferSize,
                                                 bool useLock = true,
                                                 ELogFlushPolicy* flushPolicy = nullptr,
                                                 bool printBanner = true);

    /**
     * @brief Utility method for replacing all currently configured log targets with a segmented
     * file log target.
     * @note This API call is not thread-safe, and is recommended to take place during application
     * initialization phase.
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
     * @note This API call is not thread-safe, and is recommended to take place during application
     * initialization phase.
     * @param target The target to add
     * @return ELogTargetId The resulting log target identifier or @ref ELOG_INVALID_TARGET_ID if
     * failed.
     */
    static ELogTargetId addLogTarget(ELogTarget* target);

    /**
     * @brief Utility method for adding a file log target.
     * @note This API call is not thread-safe, and is recommended to take place during application
     * initialization phase.
     * @param logFilePath The path to the log file.
     * @param bufferSize Optional buffer size for file buffering. Specify zero to disable buffering.
     * @param useLock (Relevant only for buffered logging) Optionally specify use of lock. By
     * default none is used. Pay attention that when buffering is used in a multi-threaded scenario,
     * using a lock is mandatory, and without a lock behavior is undefined.
     * @param flushPolicy Optional flush policy (if not specified then flush after each log
     * message).
     * @return ELogTargetId The resulting log target identifier or @ref ELOG_INVALID_TARGET_ID if
     * failed.
     */
    static ELogTargetId addLogFileTarget(const char* logFilePath, uint32_t bufferSize = 0,
                                         bool useLock = false,
                                         ELogFlushPolicy* flushPolicy = nullptr);

    /**
     * @brief Utility method for adding a file log target.
     * @note This API call is not thread-safe, and is recommended to take place during application
     * initialization phase.
     * @param fileHandle An open file handle. Standard output or error stream handles can be
     * specified here. The caller is responsible for closing the handle when done if needed.
     * @param bufferSize Optional buffer size for file buffering. Specify zero to disable buffering.
     * @param useLock (Relevant only for buffered logging) Optionally specify use of lock. By
     * default none is used. Pay attention that when buffering is used in a multi-threaded scenario,
     * using a lock is mandatory, and without a lock behavior is undefined.
     * @param flushPolicy Optional flush policy (if not specified then flush after each log
     * message).
     * @return ELogTargetId The resulting log target identifier or @ref ELOG_INVALID_TARGET_ID if
     * failed.
     */
    static ELogTargetId addLogFileTarget(FILE* fileHandle, uint32_t bufferSize = 0,
                                         bool useLock = false,
                                         ELogFlushPolicy* flushPolicy = nullptr);

    /**
     * @brief Utility method for adding a buffered file log target.
     * @note This API call is not thread-safe, and is recommended to take place during application
     * initialization phase.
     * @param logFilePath The path to the log file.
     * @param bufferSize Buffer size for file buffering. Cannot be zero.
     * @param useLock Optionally disable use of lock. By default a lock is used because the buffered
     * file log target is not thread-safe. If lock is disabled, and caller does not take care of
     * thread-safety then behavior is undefined.
     * @param flushPolicy Optional flush policy (if not specified then flush after each log
     * message).
     * @return ELogTargetId The resulting log target identifier or @ref ELOG_INVALID_TARGET_ID if
     * failed.
     */
    static ELogTargetId addBufferedLogFileTarget(const char* logFilePath, uint32_t bufferSize,
                                                 bool useLock = true,
                                                 ELogFlushPolicy* flushPolicy = nullptr);

    /**
     * @brief Utility method for adding a buffered file log target.
     * @note This API call is not thread-safe, and is recommended to take place during application
     * initialization phase.
     * @param fileHandle An open file handle. Standard output or error stream handles can be
     * specified here. The caller is responsible for closing the handle when done if needed.
     * @param bufferSize Buffer size for file buffering. Cannot be zero.
     * @param useLock Optionally disable use of lock. By default a lock is used because the buffered
     * file log target is not thread-safe. If lock is disabled, and caller does not take care of
     * thread-safety then behavior is undefined.
     * @param flushPolicy Optional flush policy (if not specified then flush after each log
     * message).
     * @return ELogTargetId The resulting log target identifier or @ref ELOG_INVALID_TARGET_ID if
     * failed.
     */
    static ELogTargetId addBufferedLogFileTarget(FILE* fileHandle, uint32_t bufferSize,
                                                 bool useLock = true,
                                                 ELogFlushPolicy* flushPolicy = nullptr);

    /**
     * @brief Adds a segmented file log target.
     * @note This API call is not thread-safe, and is recommended to take place during application
     * initialization phase.
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

    /** @brief Adds standard error stream log target. */
    static ELogTargetId addStdErrLogTarget();

    /** @brief Adds standard output stream log target. */
    static ELogTargetId addStdOutLogTarget();

    /** @brief Adds syslog (or Windows Event Log) target. */
    static ELogTargetId addSysLogTarget();

    /** @brief Retrieves a log target by id. Returns null if not found. */
    static ELogTarget* getLogTarget(ELogTargetId targetId);

    /** @brief Retrieves a log target by name. Returns null if not found. */
    static ELogTarget* getLogTarget(const char* logTargetName);

    /**
     * @brief Retrieves a log target id by name. Returns @ref ELOG_INVALID_TARGET_ID if not found.
     */
    static ELogTargetId getLogTargetId(const char* logTargetName);

    /**
     * @brief Removes an existing log target.
     * @note This API call is not thread-safe, and is recommended to take place during application
     * termination phase.
     * @param target The log target to remove.
     * @note It is preferable to use to other @ref removeLogTarget() method, as it runs faster.
     */
    static void removeLogTarget(ELogTarget* target);

    /**
     * @brief Removes an existing log target.
     * @note This API call is not thread-safe, and is recommended to take place during application
     * termination phase.
     * @param targetId The identifier of the log target to removed as obtained when adding or
     * setting the log target.
     */
    static void removeLogTarget(ELogTargetId targetId);

    /**
     * Log Source Management Interface
     */

    // log sources
    /**
     * @brief Defines a new log source by a qualified name if it does not already exist. If the log
     * source is already defined then no error is reported, and the existing logger is returned.
     * @note The qualified name of a log source is a name path from root to the log source,
     * separated with dots. The root source has no name nor a following dot.
     * @param qualifiedName The qualified name of the log source. This is a path from root with
     * separating dots, where root source has no name and no following dot.
     * @param defineMissingPath Optionally define all missing loggers along the name path. If not
     * specified, and a logger on the path from root to leaf is missing, then the call fails.
     * @return ELogSource The resulting log source or null if failed.
     */
    static ELogSource* defineLogSource(const char* qualifiedName, bool defineMissingPath = false);

    /** @brief Retrieves a log source by its qualified name. Returns null if not found. */
    static ELogSource* getLogSource(const char* qualifiedName);

    /** @brief Retrieves a log source by its id. Returns null if not found. */
    static ELogSource* getLogSource(ELogSourceId logSourceId);

    /** @brief Retrieves the root log source. */
    static ELogSource* getRootLogSource();

    /**
     * Logger Utility Interface
     */

    /**
     * @brief Retrieves the default logger of the elog system.
     * @note This logger is not valid before @ref  ELogSystem::initialize() is called, and not after
     * @ref ELogSystem::terminate() is called.
     * @return ELogLogger* The default logger, or null if none is defined.
     */
    static ELogLogger* getDefaultLogger();

    /**
     * @brief Retrieves a private (can be used by only one thread) logger from a log source by its
     * qualified name. The logger is managed and should not be deleted by the caller.
     */
    static ELogLogger* getPrivateLogger(const char* qualifiedSourceName);

    /**
     * @brief Retrieves a shared (can be used by more than one thread) logger from a log source by
     * its qualified name. The logger is managed and should not be deleted by the caller.
     */
    static ELogLogger* getSharedLogger(const char* qualifiedSourceName);

    /**
     * Log Formatting Interface
     */

    /**
     * @brief Configures the format of log lines.
     * @note The log line format specification is a string with normal text and white space, that
     * may contain special token references. The following special tokens are in use:
     * ${rid} - the log record id.
     * ${time} - the loging time.
     * @{host} - the host name.
     * ${user} - the logged in user.
     * ${prog} - the running program name.
     * ${pid} - the process id.
     * ${tid} - the logging thread id.
     * ${tname} - the logging thread name (requires user collaboration, see @ref
     * setCurrentThreadName()).
     * ${file} - The logging file.
     * ${line} - The logging line.
     * ${func} - The logging function.
     * ${level} - the log level
     * ${src} - the log source of the logger (qualified name).
     * ${mod} - the alternative module name associated with the source.
     * ${msg} - the log message.
     * Tokens may contain justification number, where positive means justify to the left, and
     * negative number means justify to the right. For instance: ${level:6}.
     * The list above is extendible. For further details refer to the documentation of the @ref
     * ELogFormatter class.
     * @param logFormat The log line format specification.
     * @return true If the formate specification was parsed successfully and applied, otherwise
     * false.
     */
    static bool configureLogFormat(const char* logFormat);

    /** @brief Installs a custom log formatter. */
    static void setLogFormatter(ELogFormatter* logFormatter);

    /**
     * @brief Formats a log message, using the installed log formatter.
     * @param logRecord The log record to format.
     * @param[out] logMsg The resulting formatted log message.
     */
    static void formatLogMsg(const ELogRecord& logRecord, std::string& logMsg);

    /** @brief Sets the application's name, to be referenced by token ${app}. */
    static void setAppName(const char* appName);

    /** @brief Sets the current thread's name, to be referenced by token ${tname}. */
    static void setCurrentThreadName(const char* threadName);

    /**
     * Log Filtering Interface
     */

    /** @brief Installs a custom log filter. */
    static void setLogFilter(ELogFilter* logFilter);

    /**
     * @brief Sets a global rate limit on message logging.
     * @param maxMsgPerSecond The maximum allowed number of message logging within a 1 second
     * window of time.
     * @return True if the operation succeeded, otherwise false.
     */
    static bool setRateLimit(uint32_t maxMsgPerSecond);

    /**
     * @brief Filters a log record.
     * @param logRecord The log record to filter.
     * @return true If the log record is to be processed.
     * @return false If the log record is to be discarded.
     */
    static bool filterLogMsg(const ELogRecord& logRecord);

    /**
     * Logging Interface
     */

    /**
     * @brief Logs a log record. In essence to log record is sent to all registered log targets.
     * @param logRecord The lgo record to process.
     * @param logTargetAffinityMask Optionally restricts the message to be directed to a specific
     * log target.
     */
    static void log(const ELogRecord& logRecord,
                    ELogTargetAffinityMask logTargetAffinityMask = ELOG_ALL_TARGET_AFFINITY_MASK);

#ifdef ELOG_ENABLE_STACK_TRACE
    /**
     * @brief Prints stack trace to log with the given log level.
     * @param logLevel[opt] The log level.
     * @param title[opt] The title to print before each thread stack trace.
     * @param skip[opt] The number of frames to skip.
     * @param formatter[opt] Optional stack entry formatter. Pass null to use default formatting.
     */
    static void logStackTrace(ELogLevel logLevel = ELEVEL_INFO, const char* title = "",
                              int skip = 0, dbgutil::StackEntryFormatter* formatter = nullptr);

    /**
     * @brief Prints stack trace to log with the given log level. Context is either captured by
     * calling thread, or is passed by OS through an exception/signal handler.
     * @param context[opt] OS-specific thread context. Pass null to log current thread call stack.
     * @param logLevel[opt] The log level.
     * @param title[opt] The title to print before each thread stack trace.
     * @param skip[opt] The number of frames to skip.
     * @param formatter[opt] Stack entry formatter. Pass null to use default formatting.
     */
    static void logStackTraceContext(void* context = nullptr, ELogLevel logLevel = ELEVEL_INFO,
                                     const char* title = "", int skip = 0,
                                     dbgutil::StackEntryFormatter* formatter = nullptr);

    /**
     * @brief Prints stack trace of all running threads to log with the given log level.
     * @param logLevel[opt] The log level.
     * @param title[opt] The title to print before each thread stack trace.
     * @param skip[opt] The number of frames to skip.
     * @param formatter[opt] Stack entry formatter. Pass null to use default formatting.
     */
    static void logAppStackTrace(ELogLevel logLevel = ELEVEL_INFO, const char* title = "",
                                 int skip = 0, dbgutil::StackEntryFormatter* formatter = nullptr);
#endif

    /** @brief Converts system error code to string. */
    static char* sysErrorToStr(int sysErrorCode);

#ifdef ELOG_WINDOWS
    /** @brief Converts Windows system error code to string. */
    static char* win32SysErrorToStr(unsigned long sysErrorCode);

    /** @brief Deallocates Windows system error string. */
    static void win32FreeErrorStr(char* errStr);
#endif

private:
    static bool initGlobals();
    static void termGlobals();
    static ELogSource* addChildSource(ELogSource* parent, const char* sourceName);
    static bool configureRateLimit(const std::string& rateLimitCfg);
    static bool configureLogTarget(const std::string& logTargetCfg);
    static bool configureLogTargetEx(const std::string& logTargetCfg);
    static bool configureLogTarget(const ELogConfigMapNode* logTargetCfg);
    static bool augmentConfigFromEnv(ELogConfigMapNode* cfgMap);
};

/** @brief Queries whether the default logger can log a record with a given log level. */
inline bool canLog(ELogLevel logLevel) { return ELogSystem::getDefaultLogger()->canLog(logLevel); }

}  // namespace elog

/**
 * @brief Logs a formatted message to the server log.
 * @param logger The logger used for message formatting.
 * @param level The log level. If the log level is insufficient, then the message is dropped.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#ifdef ELOG_GCC
#define ELOG_EX(logger, level, fmt, ...)                                                       \
    if (logger != nullptr && logger->canLog(level)) {                                          \
        logger->logFormat(level, __FILE__, __LINE__, __PRETTY_FUNCTION__, fmt, ##__VA_ARGS__); \
    }
#elif defined(ELOG_MSVC)
#define ELOG_EX(logger, level, fmt, ...)                                               \
    if (logger != nullptr && logger->canLog(level)) {                                  \
        logger->logFormat(level, __FILE__, __LINE__, __FUNCSIG__, fmt, ##__VA_ARGS__); \
    }
#else
#define ELOG_EX(logger, level, fmt, ...)                                            \
    if (logger != nullptr && logger->canLog(level)) {                               \
        logger->logFormat(level, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__); \
    }
#endif

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
#ifdef ELOG_GCC
#define ELOG_BEGIN_EX(logger, level, fmt, ...)                                                \
    if (logger != nullptr && logger->canLog(level)) {                                         \
        logger->startLog(level, __FILE__, __LINE__, __PRETTY_FUNCTION__, fmt, ##__VA_ARGS__); \
    }
#elif defined(ELOG_MSVC)
#define ELOG_BEGIN_EX(logger, level, fmt, ...)                                        \
    if (logger != nullptr && logger->canLog(level)) {                                 \
        logger->startLog(level, __FILE__, __LINE__, __FUNCSIG__, fmt, ##__VA_ARGS__); \
    }
#else
#define ELOG_BEGIN_EX(logger, level, fmt, ...)                                     \
    if (logger != nullptr && logger->canLog(level)) {                              \
        logger->startLog(level, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__); \
    }
#endif

/**
 * @brief Appends formatted message to a multi-part log message.
 * @param logger The logger used for message formatting.
 * @param fmt The message format.
 * @param ... The message arguments.
 */
#define ELOG_APPEND_EX(logger, fmt, ...)       \
    if (logger != nullptr) {                   \
        logger->appendLog(fmt, ##__VA_ARGS__); \
    }

/**
 * @brief Appends unformatted message to a multi-part log message.
 * @param logger The logger used for message formatting.
 * @param msg The log message.
 */
#define ELOG_APPEND_NF_EX(logger, msg)  \
    if (logger != nullptr) {            \
        logger->appendLogNoFormat(msg); \
    }

/**
 * @brief Terminates a multi-part log message and writes it to the server log.
 * @param logger The logger used for message formatting.
 */
#define ELOG_END_EX(logger)  \
    if (logger != nullptr) { \
        logger->finishLog(); \
    }

/**
 * @brief Logs a system error message to the server log.
 * @param logger The logger used for message formatting.
 * @param syscall The system call that failed.
 * @param sysErr The system error code.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_SYS_ERROR_NUM_EX(logger, syscall, sysErr, fmt, ...)                    \
    if (logger != nullptr) {                                                        \
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
#define ELOG_SYS_ERROR_EX(logger, syscall, fmt, ...)                        \
    {                                                                       \
        int sysErr = errno;                                                 \
        ELOG_SYS_ERROR_NUM_EX(logger, syscall, sysErr, fmt, ##__VA_ARGS__); \
    }

#ifdef ELOG_WINDOWS
/**
 * @brief Logs a system error message to the server log.
 * @param logger The logger used for message formatting.
 * @param syscall The system call that failed.
 * @param sysErr The system error code.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_WIN32_ERROR_NUM_EX(logger, syscall, sysErr, fmt, ...)                          \
    if (logger != nullptr) {                                                                \
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

#endif  // ELOG_WINDOWS

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
#define ELOG_SYS_ERROR(syscall, fmt, ...)                        \
    {                                                            \
        int sysErr = errno;                                      \
        ELOG_SYS_ERROR_NUM(syscall, sysErr, fmt, ##__VA_ARGS__); \
    }

#ifdef ELOG_WINDOWS
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

#endif  // ELOG_WINDOWS

#ifdef ELOG_ENABLE_STACK_TRACE
/**
 * @brief Logs the stack trace of the current thread.
 * @param logger The logger used to log.
 * @param level The log level.
 * @param title A title that may be put before the stack trace (can pass nullptr or empty string if
 * not required).
 * @param skip The number of stack frames to skip.
 * @param fmt The log message format string, that will be printed to log before the stack trace.
 * @param ... Log message format string parameters.
 */
#define ELOG_STACK_TRACE_EX(logger, level, title, skip, fmt, ...) \
    if (logger != nullptr && logger->canLog(level)) {             \
        ELOG_EX(logger, level, fmt, ##__VA_ARGS__);               \
        elog::ELogSystem::logStackTrace(level, title, skip);      \
    }

/**
 * @brief Logs the stack trace of all running threads in the application.
 * @param logger The logger used to log.
 * @param level The log level.
 * @param title A title that may be put before the stack trace of each thread (can pass nullptr or
 * empty string if not required).
 * @param skip The number of stack frames to skip.
 * @param fmt The log message format string, that will be printed to log before the stack trace.
 * @param ... Log message format string parameters.
 */
#define ELOG_APP_STACK_TRACE_EX(logger, level, title, skip, fmt, ...) \
    if (logger != nullptr && logger->canLog(level)) {                 \
        ELOG_EX(logger, level, fmt, ##__VA_ARGS__);                   \
        elog::ELogSystem::logAppStackTrace(level, title, skip);       \
    }

/**
 * @brief Logs the stack trace of the current thread (using default logger).
 * @param level The log level.
 * @param title A title that may be put before the stack trace (can pass nullptr or empty string if
 * not required).
 * @param skip The number of stack frames to skip.
 * @param fmt The log message format string, that will be printed to log before the stack trace.
 * @param ... Log message format string parameters.
 */
#define ELOG_STACK_TRACE(level, title, skip, fmt, ...)                       \
    {                                                                        \
        elog::ELogLogger* logger = elog::ELogSystem::getDefaultLogger();     \
        ELOG_STACK_TRACE_EX(logger, level, title, skip, fmt, ##__VA_ARGS__); \
    }

/**
 * @brief Logs the stack trace of all running threads in the application (using default logger).
 * @param level The log level.
 * @param title A title that may be put before the stack trace of each thread (can pass nullptr or
 * empty string if not required).
 * @param skip The number of stack frames to skip.
 * @param fmt The log message format string, that will be printed to log before the stack trace.
 * @param ... Log message format string parameters.
 */
#define ELOG_APP_STACK_TRACE(level, title, skip, fmt, ...)                       \
    {                                                                            \
        elog::ELogLogger* logger = elog::ELogSystem::getDefaultLogger();         \
        ELOG_APP_STACK_TRACE_EX(logger, level, title, skip, fmt, ##__VA_ARGS__); \
    }
#endif

/** @brief Utility macro for importing frequent names. */
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

#endif  // __ELOG_SYSTEM_H__