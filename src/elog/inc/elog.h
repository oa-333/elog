#ifndef __ELOG_SYSTEM_H__
#define __ELOG_SYSTEM_H__

#include "elog_cache.h"
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

// include fmtlib main header
#ifdef ELOG_ENABLE_FMT_LIB
// reduce noise coming from fmt lib
#ifdef ELOG_MSVC
#pragma warning(push)
#pragma warning(disable : 4582 4623 4625 4626 5027 5026)
#endif

#include <fmt/format.h>

#ifdef ELOG_MSVC
#pragma warning(pop)
#endif
#endif

/** @file The elog module facade. */

namespace elog {

/**
 * @brief Initializes the ELog library.
 * @param configFile Optional configuration file, matching the format specified by @ref
 * configureByFile().
 * @param errorHandler Optional error handler. If none specified, then all internal errors are
 * sent to the standard output stream.
 * @return true If succeeded, otherwise false.
 */
extern ELOG_API bool initialize(const char* configFile = nullptr,
                                ELogErrorHandler* errorHandler = nullptr);

/** @brief Releases all resources allocated for the ELogSystem. */
extern ELOG_API void terminate();

/** @brief Queries whether the ELog library is initialized. */
extern ELOG_API bool isInitialized();

/**
 * @brief Retrieves the logger that is used to accumulate log messages while the ELog library
 * has not initialized yet.
 */
extern ELOG_API ELogLogger* getPreInitLogger();

/**
 * @brief Discards all accumulated log messages. This will prevent from log targets added in
 * the future to receive all log messages that were accumulated before the ELog library was
 * initialized.
 */
extern ELOG_API void discardAccumulatedLogMessages();

/** @brief Installs an error handler. */
extern ELOG_API void setErrorHandler(ELogErrorHandler* errorHandler);

/** @brief Configures elog tracing. */
extern ELOG_API void setTraceMode(bool enableTrace = true);

/** @brief Queries whether trace mode is enabled. */
extern ELOG_API bool isTraceEnabled();

/** @brief Registers a schema handler by name. */
extern ELOG_API bool registerSchemaHandler(const char* schemeName,
                                           ELogSchemaHandler* schemaHandler);

/**
 * @brief Configures the ELog System from a properties configuration file.
 *
 * The expected file format is as follows:
 *
 * Each property specification appears in a single line.
 * Each property is specified as: KEY = VALUE.
 * Whitespace and empty lines are allowed.
 * Commented lines begin with '#' character (may be preceded by whitespace).
 *
 * The following properties are recognized:
 *
 * - log_format: log line format specification. See @ref configureLogFormat() for more details.
 * - rate_limit: Specified log rate limit (maximum allowed per second).
 * - log_level: expected value is any log level string (without the "ELEVEL_" prefix).
 *   Determines the global (root source) log level.
 * - <qualified-source-name>.log_level: Log level of a log source.
 * - <qualified-source-name>.log_affinity: Affinity of a log source to log targets.
 * - log_target: expected log target URL.
 *
 * Other properties are disregarded, so it is ok to put elog definitions within a larger
 * property file.
 *
 * @param props The properties map.
 * @param defineLogSources[opt] Optional parameter specifying whether each log source
 * configuration item triggers creation of the log source.
 * @param defineMissingPath[opt] In case @ref defineLogSources is true, then optionally define
 * all missing loggers along the name path. If not specified, and a logger on the path from root
 * to leaf is missing, then the call fails.
 * @return true If configuration succeeded, otherwise false.
 */
extern ELOG_API bool configureByPropFile(const char* configPath, bool defineLogSources = false,
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
extern ELOG_API bool configureByProps(const ELogPropertySequence& props,
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
extern ELOG_API bool configureByPropFileEx(const char* configPath, bool defineLogSources = false,
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
extern ELOG_API bool configureByPropsEx(const ELogPropertyPosSequence& props,
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
extern ELOG_API bool configureByFile(const char* configPath, bool defineLogSources = false,
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
extern ELOG_API bool configureByStr(const char* configStr, bool defineLogSources = false,
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
extern ELOG_API bool configure(ELogConfig* config, bool defineLogSources = false,
                               bool defineMissingPath = false);

/**
 * Log Target Management Interface
 */

/**
 * @brief Adds a log target to existing log targets.
 * @note This API call is not thread-safe, and is recommended to take place during application
 * initialization phase.
 * @param target The target to add
 * @return ELogTargetId The resulting log target identifier or @ref ELOG_INVALID_TARGET_ID if
 * failed.
 */
extern ELOG_API ELogTargetId addLogTarget(ELogTarget* target);

/**
 * @brief Configures a log target from a configuration string. This could be in URL form or as a
 * configuration string (see configuration function above for more details).
 * @note This is the recommended API for adding log targets, as it allows for adding log
 * targets, with very complex configuration, in just one connection-string/URL like parameter.
 * @param logTargetCfg The configuration string.
 * @return ELogTargetId The resulting log target identifier or @ref ELOG_INVALID_TARGET_ID if
 * failed.
 */
extern ELOG_API ELogTargetId configureLogTargetString(const char* logTargetCfg);

/**
 * @brief Adds a file log target, optionally buffered, segmented or rotating.
 * @param logFilePath The log file path (including lgo file name).
 * @param bufferSize Optional buffer size for file buffering. Specify zero to disable buffering.
 * @param useLock (Relevant only for buffered logging) Optionally specify use of lock. By
 * default none is used. If it is known that logging will take place in a thread-safe manner
 * (e.g. behind a global lock), then there is no need for the file target to make use of its own
 * lock. Pay attention that when buffering is used in a multi-threaded scenario, using a lock is
 * mandatory, and without a lock behavior is undefined.
 * @param segmentSizeMB Optionally specify a segment size limit, which will cause the log file
 * to be divided into segments. That is, when log file exceeds this limit, the log file segment
 * is closed, and a new log file segment is created.
 * @param segmentCount Optionally specify segment count limit (relevant only when a segment size
 * limit is specified). The effect of this would be a rotating log file, such that when the
 * amount of log segments has been used up, the first log segment is begin reused (thus
 * discarding old segments' log data).
 * @param logLevel Optional log level restriction. All messages with lower log level will not be
 * passed to the lgo target.
 * @param flushPolicy Optional flush policy. If not specified, no explicit flushing takes place,
 * in which case flushing takes place autonomously (e.g. when an internal buffer is full).
 * @param logFilter Optional log filter. If none specified, all messages are passed to the log
 * target.
 * @param logFormatter Optional log formatter. If none specified the global log line formatter
 * is used.
 * @return ELogTargetId The resulting log target identifier or @ref ELOG_INVALID_TARGET_ID if
 * failed.
 * @note In case of success, the log target object becomes the owner of the flush policy, filter
 * and log formatter, such that during termination, the log target is responsible for deleting
 * these objects. In case of failure to create the log target, the caller is STILL the owner of
 * these objects, and is responsible for deleting them.
 */
extern ELOG_API ELogTargetId addLogFileTarget(const char* logFilePath, uint32_t bufferSize = 0,
                                              bool useLock = false, uint32_t segmentLimitMB = 0,
                                              uint32_t segmentCount = 0,
                                              ELogLevel logLevel = ELEVEL_INFO,
                                              ELogFlushPolicy* flushPolicy = nullptr,
                                              ELogFilter* logFilter = nullptr,
                                              ELogFormatter* logFormatter = nullptr);

/**
 * @brief Adds a file log target, while attaching to an open file object.
 * @param fileHandle The open file handle. Caller is responsible for closing the file handle
 * when done, unless @ref closeHandleWhenDone is passed as true.
 * @param closeHandleWhenDone Specifies whether to close the file handle when done.
 * @param bufferSize Optional buffer size for file buffering. Specify zero to disable buffering.
 * In this case, file buffering takes place at the OS level, rather than by ELog.
 * @param useLock (Relevant only for buffered logging) Optionally specify use of lock. By
 * default none is used. If it is known that logging will take place in a thread-safe manner
 * (e.g. behind a global lock), then there is no need for the file target to make use of its own
 * lock. Pay attention that when buffering is used in a multi-threaded scenario, using a lock is
 * mandatory, and without a lock behavior is undefined.
 * @param logLevel Optional log level restriction. All messages with lower log level will not be
 * passed to the lgo target.
 * @param flushPolicy Optional flush policy. If not specified, no explicit flushing takes place,
 * in which case flushing takes place autonomously (e.g. when an internal buffer is full).
 * @param logFilter Optional log filter. If none specified, all messages are passed to the log
 * target.
 * @param logFormatter Optional log formatter. If none specified the global log line formatter
 * is used.
 * @return ELogTargetId The resulting log target identifier or @ref ELOG_INVALID_TARGET_ID if
 * failed.
 * @note In case of success, the log target object becomes the owner of the flush policy, filter
 * and log formatter, such that during termination, the log target is responsible for deleting
 * these objects. In case of failure to create the log target, the caller is STILL the owner of
 * these objects, and is responsible for deleting them.
 */
extern ELOG_API ELogTargetId attachLogFileTarget(FILE* fileHandle, bool closeHandleWhenDone = false,
                                                 uint32_t bufferSize = 0, bool useLock = false,
                                                 ELogLevel logLevel = ELEVEL_INFO,
                                                 ELogFlushPolicy* flushPolicy = nullptr,
                                                 ELogFilter* logFilter = nullptr,
                                                 ELogFormatter* logFormatter = nullptr);

/** @brief Adds standard error stream log target. */
extern ELOG_API ELogTargetId addStdErrLogTarget(ELogLevel logLevel = ELEVEL_INFO,
                                                ELogFilter* logFilter = nullptr,
                                                ELogFormatter* logFormatter = nullptr);

/** @brief Adds standard output stream log target. */
extern ELOG_API ELogTargetId addStdOutLogTarget(ELogLevel logLevel = ELEVEL_INFO,
                                                ELogFilter* logFilter = nullptr,
                                                ELogFormatter* logFormatter = nullptr);

/** @brief Adds syslog target. */
extern ELOG_API ELogTargetId addSysLogTarget(ELogLevel logLevel = ELEVEL_INFO,
                                             ELogFilter* logFilter = nullptr,
                                             ELogFormatter* logFormatter = nullptr);

/**
 * @brief Adds Windows Event Log target.
 *
 * @param logLevel Restrict reports to the event log by log level.
 * @param eventSourceName The event source name to use in the event log reports. If this
 * parameter is left empty, then the application name as configured by the user via @ref
 * setAppName() will be used. If none was set, then the program name, as extracted
 * from the current executable image, will be used instead. If all fails the name "elog" will be
 * used as a last resort.
 * @param eventId The event id to use in the event log report. Since no message
 * file/resource-dll is involved in the reports, this is solely used for searching/identifying
 * events in the event viewer.
 * @param logFilter Any additional log filter to apply to log records before reporting to the
 * event log.
 * @param logFormatter Alternate messages formatting. Since event log records already contain a
 * time stamp, it may be desired to use a simpler log format, without a time stamp.
 * @return ELogTargetId The resulting log target id, or @ref ELOG_INVALID_TARGET_ID in case of
 * failure.
 * @note Unless being explicitly overridden by the user, the Windows Event Log target can be
 * obtained by the name "win32eventlog" (see @ref getLogTarget()).
 */
extern ELOG_API ELogTargetId addWin32EventLogTarget(ELogLevel logLevel = ELEVEL_INFO,
                                                    const char* eventSourceName = "",
                                                    uint32_t eventId = 0,
                                                    ELogFilter* logFilter = nullptr,
                                                    ELogFormatter* logFormatter = nullptr);

/**
 * @brief Adds a dedicated tracer, that receives messages only from a specific logger.
 * @param traceFilePath The trace file path.
 * @param traceBufferSize The trace buffer size. If buffer is full then tracing blocks until
 * buffer has more free space (as trace messages are being written to the trace file).
 * @param targetName The log target name used for the trace target.
 * @param sourceName The log source name used to send messages to the trace target. All loggers
 * originating from this source can send messages only to the trace target (bound by target
 * affinity).
 * @return ELogTargetId The resulting log target identifier, or @ref ELOG_INVALID_TARGET_ID in
 * case of failure.
 * @note The resulting trace log target will not receive log messages from any log source except
 * for the log source configured for this target. This is done via dedicated random passkeys.
 */
extern ELOG_API ELogTargetId addTracer(const char* traceFilePath, uint32_t traceBufferSize,
                                       const char* targetName, const char* sourceName);

/** @brief Retrieves a log target by id. Returns null if not found. */
extern ELOG_API ELogTarget* getLogTarget(ELogTargetId targetId);

/** @brief Retrieves a log target by name. Returns null if not found. */
extern ELOG_API ELogTarget* getLogTarget(const char* logTargetName);

/**
 * @brief Retrieves a log target id by name. Returns @ref ELOG_INVALID_TARGET_ID if not found.
 */
extern ELOG_API ELogTargetId getLogTargetId(const char* logTargetName);

/**
 * @brief Removes an existing log target.
 * @note This API call is not thread-safe, and is recommended to take place during application
 * termination phase.
 * @param target The log target to remove.
 * @note It is preferable to use to other @ref removeLogTarget() method, as it runs faster.
 */
extern ELOG_API void removeLogTarget(ELogTarget* target);

/**
 * @brief Removes an existing log target.
 * @note This API call is not thread-safe, and is recommended to take place during application
 * termination phase.
 * @param targetId The identifier of the log target to removed as obtained when adding or
 * setting the log target.
 */
extern ELOG_API void removeLogTarget(ELogTargetId targetId);

/** @brief Removes all log targets. */
extern ELOG_API void clearAllLogTargets();

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
extern ELOG_API ELogSource* defineLogSource(const char* qualifiedName,
                                            bool defineMissingPath = false);

/** @brief Retrieves a log source by its qualified name. Returns null if not found. */
extern ELOG_API ELogSource* getLogSource(const char* qualifiedName);

/** @brief Retrieves a log source by its id. Returns null if not found. */
extern ELOG_API ELogSource* getLogSource(ELogSourceId logSourceId);

/** @brief Retrieves the root log source. */
extern ELOG_API ELogSource* getRootLogSource();

/**
 * Logger Utility Interface
 */

/**
 * @brief Retrieves the default logger of the elog system.
 * @note This logger is not valid before @ref  initialize() is called, and not after
 * @ref terminate() is called.
 * @return ELogLogger* The default logger, or null if none is defined.
 */
extern ELOG_API ELogLogger* getDefaultLogger();

/**
 * @brief Retrieves a private (can be used by only one thread) logger from a log source by its
 * qualified name. The logger is managed and should not be deleted by the caller.
 * @note This call is NOT thread safe.
 */
extern ELOG_API ELogLogger* getPrivateLogger(const char* qualifiedSourceName);

/**
 * @brief Retrieves a shared (can be used by more than one thread) logger from a log source by
 * its qualified name. The logger is managed and should not be deleted by the caller.
 * @note This call is NOT thread safe.
 */
extern ELOG_API ELogLogger* getSharedLogger(const char* qualifiedSourceName);

/**
 * Log Level Interface
 */

/**
 * @brief Retrieves the global log level (the log level of the root log source).
 * @return The log level.
 */
extern ELOG_API ELogLevel getLogLevel();

/** @brief Configures the global log level. */

/**
 * @brief Set the global log level of the root log source.
 *
 * @param logLevel Th elog level to set.
 * @param propagateMode Specifies how the log level should propagate to child log sources of the
 * root log source.
 */
extern ELOG_API void setLogLevel(ELogLevel logLevel, ELogPropagateMode propagateMode);

/**
 * @brief Set the font and/or color of a specific log level.
 *
 * @param logLevel The log level to configure.
 * @param formatSpec The font/color specification for the log level.
 */
// extern ELOG_API void setLogLevelFormat(ELogLevel logLevel, const ELogTextSpec& formatSpec);

/**
 * @brief Configures the font/color of several log level using a configuration string, with the
 * following format:
 *
 * "{<level>:<spec>, <level>:<spec>}"
 *
 * Where level is any of the log levels (without ELOG_ prefix, e.g. INFO), and font/color
 * specification is a  follows any of these formats:
 *
 * fg/bg-color = <any of black, red, green, yellow, blue, magenta, cyan, white>
 * fg/bg-color = #RRGGBB (full hexadecimal RGB specification)
 * fg/bg-color = vga#RRGGBB (vga hexadecimal RGB color, each component may not exceed 1F)
 * fg/bg-color = grey/gray#<value>> (grayscale value in the range 0-23, inclusive, zero is dark)
 * font = bold/faint/italic/underline/cross-out/blink-slow/blink-rapid
 *
 * Font format specification may appear more than once. For instance:
 *
 * {TRACE:font=faint, INFO:fg-color=green, WARN:fg-color=yellow,
 * ERROR:fg-color=red:font=bold:font=blink-rapid}
 *
 * Note that all simple colors may be preceded by "bright-" prefix (e.g. bright-yellow).
 *
 * @param logLevelConfig log level format configuration.
 * @return True if the operation succeeded, otherwise false (i.e. parse error).
 */
// extern ELOG_API bool configureLogLevelFormat(const char* logLevelConfig);

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
extern ELOG_API bool configureLogFormat(const char* logFormat);

/** @brief Installs a custom log formatter. */
extern ELOG_API void setLogFormatter(ELogFormatter* logFormatter);

/**
 * @brief Formats a log message, using the installed log formatter.
 * @param logRecord The log record to format.
 * @param[out] logMsg The resulting formatted log message.
 */
extern ELOG_API void formatLogMsg(const ELogRecord& logRecord, std::string& logMsg);

/**
 * @brief Formats a log message, using the installed log formatter.
 * @param logRecord The log record to format.
 * @param[out] logBuffer The resulting formatted log buffer.
 */
extern ELOG_API void formatLogBuffer(const ELogRecord& logRecord, ELogBuffer& logBuffer);

/** @brief Sets the application's name, to be referenced by token ${app}. */
extern ELOG_API void setAppName(const char* appName);

/** @brief Sets the current thread's name, to be referenced by token ${tname}. */
extern ELOG_API void setCurrentThreadName(const char* threadName);

/**
 * Log Filtering Interface
 */

/** @brief Configures top-level log filter form configuration string. */
extern ELOG_API bool configureLogFilter(const char* logFilterCfg);

/** @brief Installs a custom log filter. */
extern ELOG_API void setLogFilter(ELogFilter* logFilter);

/**
 * @brief Sets a global rate limit on message logging.
 * @param maxMsgPerSecond The maximum allowed number of message logging within a 1 second
 * window of time.
 * @param replaceGlobalFilter Specified what to do in case of an existing global log filter. If
 * set to true, then the rate limiter will replace any configured global log filter. If set to
 * false then the rate limiter will be combined with the currently configured global log filter
 * using OR operator. In no global filter is currently being used then this parameter has no
 * significance and is ignored.
 * @return True if the operation succeeded, otherwise false.
 * @note Setting the global rate limit will replace the current global log filter. If the
 * intention is to add a rate limiting to the current log filter
 */
extern ELOG_API bool setRateLimit(uint32_t maxMsgPerSecond, bool replaceGlobalFilter = true);

/**
 * @brief Filters a log record.
 * @param logRecord The log record to filter.
 * @return true If the log record is to be processed.
 * @return false If the log record is to be discarded.
 */
extern ELOG_API bool filterLogMsg(const ELogRecord& logRecord);

/**
 * Logging Interface
 */

/**
 * @brief Logs a log record. In essence to log record is sent to all registered log targets.
 * @param logRecord The lgo record to process.
 * @param logTargetAffinityMask Optionally restricts the message to be directed to a specific
 * log target.
 */
extern ELOG_API void logMsg(
    const ELogRecord& logRecord,
    ELogTargetAffinityMask logTargetAffinityMask = ELOG_ALL_TARGET_AFFINITY_MASK);

#ifdef ELOG_ENABLE_STACK_TRACE
/**
 * @brief Prints stack trace to log with the given log level.
 * @param logger The logger to use for printing the stack trace.
 * @param logLevel[opt] The log level.
 * @param title[opt] The title to print before each thread stack trace.
 * @param skip[opt] The number of frames to skip.
 * @param formatter[opt] Optional stack entry formatter. Pass null to use default formatting.
 */
extern ELOG_API void logStackTrace(ELogLogger* logger, ELogLevel logLevel = ELEVEL_INFO,
                                   const char* title = "", int skip = 0,
                                   dbgutil::StackEntryFormatter* formatter = nullptr);

/**
 * @brief Prints stack trace to log with the given log level. Context is either captured by
 * calling thread, or is passed by OS through an exception/signal handler.
 * @param logger The logger to use for printing the stack trace.
 * @param context[opt] OS-specific thread context. Pass null to log current thread call stack.
 * @param logLevel[opt] The log level.
 * @param title[opt] The title to print before each thread stack trace.
 * @param skip[opt] The number of frames to skip.
 * @param formatter[opt] Stack entry formatter. Pass null to use default formatting.
 */
extern ELOG_API void logStackTraceContext(ELogLogger* logger, void* context = nullptr,
                                          ELogLevel logLevel = ELEVEL_INFO, const char* title = "",
                                          int skip = 0,
                                          dbgutil::StackEntryFormatter* formatter = nullptr);

/**
 * @brief Prints stack trace of all running threads to log with the given log level.
 * @param logger The logger to use for printing the stack trace.
 * @param logLevel[opt] The log level.
 * @param title[opt] The title to print before each thread stack trace.
 * @param skip[opt] The number of frames to skip.
 * @param formatter[opt] Stack entry formatter. Pass null to use default formatting.
 */
extern ELOG_API void logAppStackTrace(ELogLogger* logger, ELogLevel logLevel = ELEVEL_INFO,
                                      const char* title = "", int skip = 0,
                                      dbgutil::StackEntryFormatter* formatter = nullptr);
#endif

/** @brief Converts system error code to string. */
extern ELOG_API char* sysErrorToStr(int sysErrorCode);

#ifdef ELOG_WINDOWS
/** @brief Converts Windows system error code to string. */
extern ELOG_API char* win32SysErrorToStr(unsigned long sysErrorCode);

/** @brief Deallocates Windows system error string. */
extern ELOG_API void win32FreeErrorStr(char* errStr);
#endif

/*private:*/

inline ELogLogger* getValidLogger(ELogLogger* logger) {
    if (logger != nullptr) {
        return logger;
    } else if (elog::isInitialized()) {
        return elog::getDefaultLogger();
    } else {
        return elog::getPreInitLogger();
    }
}

/** @brief Queries whether the default logger can log a record with a given log level. */
inline bool canLog(ELogLevel logLevel) { return getValidLogger(nullptr)->canLog(logLevel); }

}  // namespace elog

/** @def Define a unified function name macro */
#ifdef ELOG_GCC
#define ELOG_FUNCTION __PRETTY_FUNCTION__
#elif defined(ELOG_MSVC)
#define ELOG_FUNCTION __FUNCSIG__
#else
#define ELOG_FUNCTION __func__
#endif

/**
 * @brief Logs a formatted message to the server log.
 * @param logger The logger used for message formatting.
 * @param level The log level. If the log level is insufficient, then the message is dropped.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_EX(logger, level, fmt, ...)                                                          \
    {                                                                                             \
        elog::ELogLogger* validLogger = elog::getValidLogger(logger);                             \
        if (validLogger->canLog(level)) {                                                         \
            validLogger->logFormat(level, __FILE__, __LINE__, ELOG_FUNCTION, fmt, ##__VA_ARGS__); \
        }                                                                                         \
    }

#ifdef ELOG_ENABLE_FMT_LIB
/** @brief Logs a formatted message to the server log (fmtlib style). */
#define ELOG_FMT_EX(logger, level, fmtStr, ...)                                                 \
    {                                                                                           \
        elog::ELogLogger* validLogger = elog::getValidLogger(logger);                           \
        if (validLogger->canLog(level)) {                                                       \
            std::string logMsg = fmt::format(fmtStr, ##__VA_ARGS__);                            \
            validLogger->logNoFormat(level, __FILE__, __LINE__, ELOG_FUNCTION, logMsg.c_str()); \
        }                                                                                       \
    }

/** @brief Logs a formatted message to the server log (fmtlib style, binary form). */
#define ELOG_BIN_EX(logger, level, fmt, ...)                                                      \
    {                                                                                             \
        elog::ELogLogger* validLogger = elog::getValidLogger(logger);                             \
        if (validLogger->canLog(level)) {                                                         \
            validLogger->logBinary(level, __FILE__, __LINE__, ELOG_FUNCTION, fmt, ##__VA_ARGS__); \
        }                                                                                         \
    }

/** @brief Logs a formatted message to the server log (fmtlib style, binary form, auto-cached). */
#define ELOG_CACHE_EX(logger, level, fmt, ...)                                                   \
    {                                                                                            \
        elog::ELogLogger* validLogger = elog::getValidLogger(logger);                            \
        if (validLogger->canLog(level)) {                                                        \
            static thread_local elog::ELogCacheEntryId cacheEntryId =                            \
                elog::ELogCache::getOrCacheFormatMsg(fmt);                                       \
            validLogger->logBinaryCached(level, __FILE__, __LINE__, ELOG_FUNCTION, cacheEntryId, \
                                         ##__VA_ARGS__);                                         \
        }                                                                                        \
    }

/** @brief Logs a formatted message to the server log (fmtlib style, binary form, pre-cached). */
#define ELOG_ID_EX(logger, level, cacheEntryId, ...)                                             \
    {                                                                                            \
        elog::ELogLogger* validLogger = elog::getValidLogger(logger);                            \
        if (validLogger->canLog(level)) {                                                        \
            validLogger->logBinaryCached(level, __FILE__, __LINE__, ELOG_FUNCTION, cacheEntryId, \
                                         ##__VA_ARGS__);                                         \
        }                                                                                        \
    }
#endif

/**
 * @brief Logs a fatal message to the server log.
 * @param logger The logger used for message formatting.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_FATAL_EX(logger, fmt, ...) ELOG_EX(logger, elog::ELEVEL_FATAL, fmt, ##__VA_ARGS__)

#ifdef ELOG_ENABLE_FMT_LIB
/** @brief Logs a fatal message to the server log (fmtlib style). */
#define ELOG_FMT_FATAL_EX(logger, fmt, ...) \
    ELOG_FMT_EX(logger, elog::ELEVEL_FATAL, fmt, ##__VA_ARGS__)

/** @brief Logs a fatal message to the server log (fmtlib style, binary form). */
#define ELOG_BIN_FATAL_EX(logger, fmt, ...) \
    ELOG_BIN_EX(logger, elog::ELEVEL_FATAL, fmt, ##__VA_ARGS__)

/** @brief Logs a fatal message to the server log (fmtlib style, binary form, auto-cached). */
#define ELOG_CACHE_FATAL_EX(logger, fmt, ...) \
    ELOG_CACHE_EX(logger, elog::ELEVEL_FATAL, fmt, ##__VA_ARGS__)

/** @brief Logs a fatal message to the server log (fmtlib style, binary form, pre-cached). */
#define ELOG_ID_FATAL_EX(logger, cacheEntryId, ...) \
    ELOG_ID_EX(logger, elog::ELEVEL_FATAL, cacheEntryId, ##__VA_ARGS__)
#endif

/**
 * @brief Logs an error message to the server log.
 * @param logger The logger used for message formatting.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_ERROR_EX(logger, fmt, ...) ELOG_EX(logger, elog::ELEVEL_ERROR, fmt, ##__VA_ARGS__)

#ifdef ELOG_ENABLE_FMT_LIB
/** @brief Logs a error message to the server log (fmtlib style). */
#define ELOG_FMT_ERROR_EX(logger, fmt, ...) \
    ELOG_FMT_EX(logger, elog::ELEVEL_ERROR, fmt, ##__VA_ARGS__)

/** @brief Logs a error message to the server log (fmtlib style, binary form). */
#define ELOG_BIN_ERROR_EX(logger, fmt, ...) \
    ELOG_BIN_EX(logger, elog::ELEVEL_ERROR, fmt, ##__VA_ARGS__)

/** @brief Logs a error message to the server log (fmtlib style, binary form, auto-cached). */
#define ELOG_CACHE_ERROR_EX(logger, fmt, ...) \
    ELOG_CACHE_EX(logger, elog::ELEVEL_ERROR, fmt, ##__VA_ARGS__)

/** @brief Logs a error message to the server log (fmtlib style, binary form, pre-cached). */
#define ELOG_ID_ERROR_EX(logger, cacheEntryId, ...) \
    ELOG_ID_EX(logger, elog::ELEVEL_ERROR, cacheEntryId, ##__VA_ARGS__)
#endif

/**
 * @brief Logs a warning message to the server log.
 * @param logger The logger used for message formatting.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_WARN_EX(logger, fmt, ...) ELOG_EX(logger, elog::ELEVEL_WARN, fmt, ##__VA_ARGS__)

#ifdef ELOG_ENABLE_FMT_LIB
/** @brief Logs a warning message to the server log (fmtlib style). */
#define ELOG_FMT_WARN_EX(logger, fmt, ...) \
    ELOG_FMT_EX(logger, elog::ELEVEL_WARN, fmt, ##__VA_ARGS__)

/** @brief Logs a warning message to the server log (fmtlib style, binary form). */
#define ELOG_BIN_WARN_EX(logger, fmt, ...) \
    ELOG_BIN_EX(logger, elog::ELEVEL_WARN, fmt, ##__VA_ARGS__)

/** @brief Logs a warning message to the server log (fmtlib style, binary form, auto-cached). */
#define ELOG_CACHE_WARN_EX(logger, fmt, ...) \
    ELOG_CACHE_EX(logger, elog::ELEVEL_WARN, fmt, ##__VA_ARGS__)

/** @brief Logs a warning message to the server log (fmtlib style, binary form, pre-cached). */
#define ELOG_ID_WARN_EX(logger, cacheEntryId, ...) \
    ELOG_ID_EX(logger, elog::ELEVEL_WARN, cacheEntryId, ##__VA_ARGS__)
#endif

/**
 * @brief Logs a notice message to the server log.
 * @param logger The logger used for message formatting.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_NOTICE_EX(logger, fmt, ...) ELOG_EX(logger, elog::ELEVEL_NOTICE, fmt, ##__VA_ARGS__)

#ifdef ELOG_ENABLE_FMT_LIB
/** @brief Logs a notice message to the server log (fmtlib style). */
#define ELOG_FMT_NOTICE_EX(logger, fmt, ...) \
    ELOG_FMT_EX(logger, elog::ELEVEL_NOTICE, fmt, ##__VA_ARGS__)

/** @brief Logs a notice message to the server log (fmtlib style, binary form). */
#define ELOG_BIN_NOTICE_EX(logger, fmt, ...) \
    ELOG_BIN_EX(logger, elog::ELEVEL_NOTICE, fmt, ##__VA_ARGS__)

/** @brief Logs a notice message to the server log (fmtlib style, binary form, auto-cached). */
#define ELOG_CACHE_NOTICE_EX(logger, fmt, ...) \
    ELOG_CACHE_EX(logger, elog::ELEVEL_NOTICE, fmt, ##__VA_ARGS__)

/** @brief Logs a notice message to the server log (fmtlib style, binary form, pre-cached). */
#define ELOG_ID_NOTICE_EX(logger, cacheEntryId, ...) \
    ELOG_ID_EX(logger, elog::ELEVEL_NOTICE, cacheEntryId, ##__VA_ARGS__)
#endif

/**
 * @brief Logs an informational message to the server log.
 * @param logger The logger used for message formatting.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_INFO_EX(logger, fmt, ...) ELOG_EX(logger, elog::ELEVEL_INFO, fmt, ##__VA_ARGS__)

#ifdef ELOG_ENABLE_FMT_LIB
/** @brief Logs an informational message to the server log (fmtlib style). */
#define ELOG_FMT_INFO_EX(logger, fmt, ...) \
    ELOG_FMT_EX(logger, elog::ELEVEL_INFO, fmt, ##__VA_ARGS__)

/** @brief Logs an informational message to the server log (fmtlib style, binary form). */
#define ELOG_BIN_INFO_EX(logger, fmt, ...) \
    ELOG_BIN_EX(logger, elog::ELEVEL_INFO, fmt, ##__VA_ARGS__)

/** @brief Logs an informational message to the server log (fmtlib style, binary form, auto-cached).
 */
#define ELOG_CACHE_INFO_EX(logger, fmt, ...) \
    ELOG_CACHE_EX(logger, elog::ELEVEL_INFO, fmt, ##__VA_ARGS__)

/**
 * @brief Logs a informational message to the server log (fmtlib style, binary form, pre-cached).
 */
#define ELOG_ID_INFO_EX(logger, cacheEntryId, ...) \
    ELOG_ID_EX(logger, elog::ELEVEL_INFO, cacheEntryId, ##__VA_ARGS__)
#endif

/**
 * @brief Logs a trace message to the server log.
 * @param logger The logger used for message formatting.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_TRACE_EX(logger, fmt, ...) ELOG_EX(logger, elog::ELEVEL_TRACE, fmt, ##__VA_ARGS__)

#ifdef ELOG_ENABLE_FMT_LIB
/** @brief Logs a trace message to the server log (fmtlib style). */
#define ELOG_FMT_TRACE_EX(logger, fmt, ...) \
    ELOG_FMT_EX(logger, elog::ELEVEL_TRACE, fmt, ##__VA_ARGS__)

/** @brief Logs a trace message to the server log (fmtlib style, binary form). */
#define ELOG_BIN_TRACE_EX(logger, fmt, ...) \
    ELOG_BIN_EX(logger, elog::ELEVEL_TRACE, fmt, ##__VA_ARGS__)

/** @brief Logs a trace message to the server log (fmtlib style, binary form, auto-cached). */
#define ELOG_CACHE_TRACE_EX(logger, fmt, ...) \
    ELOG_CACHE_EX(logger, elog::ELEVEL_TRACE, fmt, ##__VA_ARGS__)

/** @brief Logs a trace message to the server log (fmtlib style, binary form, pre-cached). */
#define ELOG_ID_TRACE_EX(logger, cacheEntryId, ...) \
    ELOG_ID_EX(logger, elog::ELEVEL_TRACE, cacheEntryId, ##__VA_ARGS__)
#endif

/**
 * @brief Logs a debug message to the server log.
 * @param logger The logger used for message formatting.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_DEBUG_EX(logger, fmt, ...) ELOG_EX(logger, elog::ELEVEL_DEBUG, fmt, ##__VA_ARGS__)

#ifdef ELOG_ENABLE_FMT_LIB
/** @brief Logs a debug message to the server log (fmtlib style). */
#define ELOG_FMT_DEBUG_EX(logger, fmt, ...) \
    ELOG_FMT_EX(logger, elog::ELEVEL_DEBUG, fmt, ##__VA_ARGS__)

/** @brief Logs a debug message to the server log (fmtlib style, binary form). */
#define ELOG_BIN_DEBUG_EX(logger, fmt, ...) \
    ELOG_BIN_EX(logger, elog::ELEVEL_DEBUG, fmt, ##__VA_ARGS__)

/** @brief Logs a debug message to the server log (fmtlib style, binary form, auto-cached). */
#define ELOG_CACHE_DEBUG_EX(logger, fmt, ...) \
    ELOG_CACHE_EX(logger, elog::ELEVEL_DEBUG, fmt, ##__VA_ARGS__)

/** @brief Logs a debug message to the server log (fmtlib style, binary form, pre-cached). */
#define ELOG_ID_DEBUG_EX(logger, cacheEntryId, ...) \
    ELOG_ID_EX(logger, elog::ELEVEL_DEBUG, cacheEntryId, ##__VA_ARGS__)
#endif

/**
 * @brief Logs a diagnostic message to the server log.
 * @param logger The logger used for message formatting.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_DIAG_EX(logger, fmt, ...) ELOG_EX(logger, elog::ELEVEL_DIAG, fmt, ##__VA_ARGS__)

#ifdef ELOG_ENABLE_FMT_LIB
/** @brief Logs a diagnostic message to the server log (fmtlib style). */
#define ELOG_FMT_DIAG_EX(logger, fmt, ...) \
    ELOG_FMT_EX(logger, elog::ELEVEL_DIAG, fmt, ##__VA_ARGS__)

/** @brief Logs a diagnostic message to the server log (fmtlib style, binary form). */
#define ELOG_BIN_DIAG_EX(logger, fmt, ...) \
    ELOG_BIN_EX(logger, elog::ELEVEL_DIAG, fmt, ##__VA_ARGS__)

/** @brief Logs a diagnostic message to the server log (fmtlib style, binary form, auto-cached). */
#define ELOG_CACHE_DIAG_EX(logger, fmt, ...) \
    ELOG_CACHE_EX(logger, elog::ELEVEL_DIAG, fmt, ##__VA_ARGS__)

/** @brief Logs a diagnostic message to the server log (fmtlib style, binary form, pre-cached). */
#define ELOG_ID_DIAG_EX(logger, cacheEntryId, ...) \
    ELOG_ID_EX(logger, elog::ELEVEL_DIAG, cacheEntryId, ##__VA_ARGS__)
#endif

/**
 * @brief Begins a multi-part log message.
 * @param logger The logger used for message formatting.
 * @param level The log level. If the log level is insufficient, then the message is dropped.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_BEGIN_EX(logger, level, fmt, ...)                                                   \
    {                                                                                            \
        elog::ELogLogger* validLogger = elog::getValidLogger(logger);                            \
        if (validLogger->canLog(level)) {                                                        \
            validLogger->startLog(level, __FILE__, __LINE__, ELOG_FUNCTION, fmt, ##__VA_ARGS__); \
        }                                                                                        \
    }

/** @brief Begins a multi-part log message (fmtlib style). */
#ifdef ELOG_ENABLE_FMT_LIB
#define ELOG_FMT_BEGIN_EX(logger, level, fmtStr, ...)                               \
    {                                                                               \
        elog::ELogLogger* validLogger = elog::getValidLogger(logger);               \
        if (validLogger->canLog(level)) {                                           \
            std::string logMsg = fmt::format(fmtStr, ##__VA_ARGS__);                \
            validLogger->startLogNoFormat(level, __FILE__, __LINE__, ELOG_FUNCTION, \
                                          logMsg.c_str());                          \
        }                                                                           \
    }
#endif

/**
 * @brief Appends formatted message to a multi-part log message.
 * @param logger The logger used for message formatting.
 * @param fmt The message format.
 * @param ... The message arguments.
 */
#define ELOG_APPEND_EX(logger, fmt, ...) elog::getValidLogger(logger)->appendLog(fmt, ##__VA_ARGS__)

/** @brief Appends formatted message to a multi-part log message (fmtlib style). */
#ifdef ELOG_ENABLE_FMT_LIB
#define ELOG_FMT_APPEND_EX(logger, fmtStr, ...)                          \
    {                                                                    \
        std::string logMsg = fmt::format(fmtStr, ##__VA_ARGS__);         \
        elog::getValidLogger(logger)->appendLogNoFormat(logMsg.c_str()); \
    }
#endif

/**
 * @brief Appends unformatted message to a multi-part log message.
 * @param logger The logger used for message formatting.
 * @param msg The log message.
 */
#define ELOG_APPEND_NF_EX(logger, msg) elog::getValidLogger(logger)->appendLogNoFormat(msg)

/**
 * @brief Terminates a multi-part log message and writes it to the server log.
 * @param logger The logger used for message formatting.
 */
#define ELOG_END_EX(logger) elog::getValidLogger(logger)->finishLog()

/**
 * @brief Logs a system error message to the server log.
 * @param logger The logger used for message formatting.
 * @param syscall The system call that failed.
 * @param sysErr The system error code.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_SYS_ERROR_NUM_EX(logger, syscall, sysErr, fmt, ...)                         \
    {                                                                                    \
        elog::ELogLogger* validLogger = elog::getValidLogger(logger);                    \
        ELOG_ERROR_EX(validLogger, "System call " #syscall "() failed: %d (%s)", sysErr, \
                      elog::sysErrorToStr(sysErr));                                      \
        ELOG_ERROR_EX(validLogger, fmt, ##__VA_ARGS__);                                  \
    }

/** @brief Logs a system error message to the server log (fmtlib style). */
#ifdef ELOG_ENABLE_FMT_LIB
#define ELOG_FMT_SYS_ERROR_NUM_EX(logger, syscall, sysErr, fmt, ...)                     \
    {                                                                                    \
        elog::ELogLogger* validLogger = elog::getValidLogger(logger);                    \
        ELOG_ERROR_EX(validLogger, "System call " #syscall "() failed: %d (%s)", sysErr, \
                      elog::sysErrorToStr(sysErr));                                      \
        ELOG_FMT_ERROR_EX(validLogger, fmt, ##__VA_ARGS__);                              \
    }
#endif

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

/** @brief Logs a system error message to the server log (fmtlib style). */
#ifdef ELOG_ENABLE_FMT_LIB
#define ELOG_FMT_SYS_ERROR_EX(logger, syscall, fmt, ...)                        \
    {                                                                           \
        int sysErr = errno;                                                     \
        ELOG_FMT_SYS_ERROR_NUM_EX(logger, syscall, sysErr, fmt, ##__VA_ARGS__); \
    }
#endif

#ifdef ELOG_WINDOWS
/**
 * @brief Logs a system error message to the server log.
 * @param logger The logger used for message formatting.
 * @param syscall The system call that failed.
 * @param sysErr The system error code.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_WIN32_ERROR_NUM_EX(logger, syscall, sysErr, fmt, ...)                              \
    {                                                                                           \
        elog::ELogLogger* errLogger = elog::getValidLogger(logger);                             \
        char* errStr = elog::win32SysErrorToStr(sysErr);                                        \
        ELOG_ERROR_EX(errLogger, "Windows system call " #syscall "() failed: %lu (%s)", sysErr, \
                      errStr);                                                                  \
        elog::win32FreeErrorStr(errStr);                                                        \
        ELOG_ERROR_EX(errLogger, fmt, ##__VA_ARGS__);                                           \
    }

/** @brief Logs a system error message to the server log (fmtlib style). */
#ifdef ELOG_ENABLE_FMT_LIB
#define ELOG_FMT_WIN32_ERROR_NUM_EX(logger, syscall, sysErr, fmt, ...)                         \
    {                                                                                          \
        elog::ELogLogger* errLogger = elog::getValidLogger(logger);                            \
        char* errStr = elog::win32SysErrorToStr(sysErr);                                       \
        ELOG_ERROR_EX(errLogger, "Windows system call " #syscall "() failed: %d (%s)", sysErr, \
                      errStr);                                                                 \
        elog::win32FreeErrorStr(errStr);                                                       \
        ELOG_FMT_ERROR_EX(errLogger, fmt, ##__VA_ARGS__);                                      \
    }
#endif

/**
 * @brief Logs a system error message to the server log.
 * @param logger The logger used for message formatting.
 * @param syscall The system call that failed.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 * @note The error code for the system call is obtained through @ref errno. If you wish to provide
 * another error code then consider calling @ref ELOG_SYS_ERROR_NUM() instead.
 */
#define ELOG_WIN32_ERROR_EX(logger, syscall, fmt, ...)                        \
    {                                                                         \
        DWORD sysErr = ::GetLastError();                                      \
        ELOG_WIN32_ERROR_NUM_EX(logger, syscall, sysErr, fmt, ##__VA_ARGS__); \
    }

/** @brief Logs a system error message to the server log (fmtlib style). */
#ifdef ELOG_ENABLE_FMT_LIB
#define ELOG_FMT_WIN32_ERROR_EX(logger, syscall, fmt, ...)                        \
    {                                                                             \
        DWORD sysErr = ::GetLastError();                                          \
        ELOG_FMT_WIN32_ERROR_NUM_EX(logger, syscall, sysErr, fmt, ##__VA_ARGS__); \
    }
#endif

#endif  // ELOG_WINDOWS

/**
 * @brief Logs a formatted message to the server log (using default logger).
 * @param level The log level. If the log level is insufficient, then the message is dropped.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG(level, fmt, ...)                                \
    {                                                        \
        elog::ELogLogger* logger = elog::getDefaultLogger(); \
        ELOG_EX(logger, level, fmt, ##__VA_ARGS__);          \
    }

#ifdef ELOG_ENABLE_FMT_LIB
/** @brief Logs a formatted message to the server log, using default logger (fmtlib-style). */
#define ELOG_FMT(level, fmt, ...)                            \
    {                                                        \
        elog::ELogLogger* logger = elog::getDefaultLogger(); \
        ELOG_FMT_EX(logger, level, fmt, ##__VA_ARGS__);      \
    }

/**
 * @brief Logs a formatted message to the server log, using default logger (fmtlib-style, binary
 * form).
 */
#define ELOG_BIN(level, fmt, ...)                            \
    {                                                        \
        elog::ELogLogger* logger = elog::getDefaultLogger(); \
        ELOG_BIN_EX(logger, level, fmt, ##__VA_ARGS__);      \
    }

/**
 * @brief Logs a formatted message to the server log, using default logger (fmtlib-style, binary
 * form, auto-cached).
 */
#define ELOG_CACHE(level, fmt, ...)                          \
    {                                                        \
        elog::ELogLogger* logger = elog::getDefaultLogger(); \
        ELOG_CACHE_EX(logger, level, fmt, ##__VA_ARGS__);    \
    }

/**
 * @brief Logs a formatted message to the server log, using default logger (fmtlib-style, binary
 * form, pre-cached).
 */
#define ELOG_ID(level, cacheEntryId, ...)                       \
    {                                                           \
        elog::ELogLogger* logger = elog::getDefaultLogger();    \
        ELOG_ID_EX(logger, level, cacheEntryId, ##__VA_ARGS__); \
    }
#endif

/**
 * @brief Logs a fatal message to the server log.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_FATAL(fmt, ...) ELOG(elog::ELEVEL_FATAL, fmt, ##__VA_ARGS__)

#ifdef ELOG_ENABLE_FMT_LIB
/** @brief Logs a fatal message to the server log (fmtlib style). */
#define ELOG_FMT_FATAL(fmt, ...) ELOG_FMT(elog::ELEVEL_FATAL, fmt, ##__VA_ARGS__)

/** @brief Logs a fatal message to the server log (fmtlib style, binary form). */
#define ELOG_BIN_FATAL(fmt, ...) ELOG_BIN(elog::ELEVEL_FATAL, fmt, ##__VA_ARGS__)

/** @brief Logs a fatal message to the server log (fmtlib style, binary form, auto-cached). */
#define ELOG_CACHE_FATAL(fmt, ...) ELOG_CACHE(elog::ELEVEL_FATAL, fmt, ##__VA_ARGS__)

/** @brief Logs a fatal message to the server log (fmtlib style, binary form, pre-cached). */
#define ELOG_ID_FATAL(cacheEntryId, ...) ELOG_ID(elog::ELEVEL_FATAL, cacheEntryId, ##__VA_ARGS__)
#endif

/**
 * @brief Logs an error message to the server log.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_ERROR(fmt, ...) ELOG(elog::ELEVEL_ERROR, fmt, ##__VA_ARGS__)

#ifdef ELOG_ENABLE_FMT_LIB
/** @brief Logs a error message to the server log (fmtlib style). */
#define ELOG_FMT_ERROR(fmt, ...) ELOG_FMT(elog::ELEVEL_ERROR, fmt, ##__VA_ARGS__)

/** @brief Logs a error message to the server log (fmtlib style, binary form). */
#define ELOG_BIN_ERROR(fmt, ...) ELOG_BIN(elog::ELEVEL_ERROR, fmt, ##__VA_ARGS__)

/** @brief Logs a error message to the server log (fmtlib style, binary form, auto-cached). */
#define ELOG_CACHE_ERROR(fmt, ...) ELOG_CACHE(elog::ELEVEL_ERROR, fmt, ##__VA_ARGS__)

/** @brief Logs a error message to the server log (fmtlib style, binary form, pre-cached). */
#define ELOG_ID_ERROR(cacheEntryId, ...) ELOG_ID(elog::ELEVEL_ERROR, cacheEntryId, ##__VA_ARGS__)
#endif

/**
 * @brief Logs a warning message to the server log.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_WARN(fmt, ...) ELOG(elog::ELEVEL_WARN, fmt, ##__VA_ARGS__)

#ifdef ELOG_ENABLE_FMT_LIB
/** @brief Logs a warning message to the server log (fmtlib style). */
#define ELOG_FMT_WARN(fmt, ...) ELOG_FMT(elog::ELEVEL_WARN, fmt, ##__VA_ARGS__)

/** @brief Logs a warning message to the server log (fmtlib style, binary form). */
#define ELOG_BIN_WARN(fmt, ...) ELOG_BIN(elog::ELEVEL_WARN, fmt, ##__VA_ARGS__)

/** @brief Logs a warning message to the server log (fmtlib style, binary form, auto-cached). */
#define ELOG_CACHE_WARN(fmt, ...) ELOG_CACHE(elog::ELEVEL_WARN, fmt, ##__VA_ARGS__)

/** @brief Logs a warning message to the server log (fmtlib style, binary form, pre-cached). */
#define ELOG_ID_WARN(cacheEntryId, ...) ELOG_ID(elog::ELEVEL_WARN, cacheEntryId, ##__VA_ARGS__)
#endif

/**
 * @brief Logs a notice message to the server log.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_NOTICE(fmt, ...) ELOG(elog::ELEVEL_NOTICE, fmt, ##__VA_ARGS__)

#ifdef ELOG_ENABLE_FMT_LIB
/** @brief Logs a notice message to the server log (fmtlib style). */
#define ELOG_FMT_NOTICE(fmt, ...) ELOG_FMT(elog::ELEVEL_NOTICE, fmt, ##__VA_ARGS__)

/** @brief Logs a notice message to the server log (fmtlib style, binary form). */
#define ELOG_BIN_NOTICE(fmt, ...) ELOG_BIN(elog::ELEVEL_NOTICE, fmt, ##__VA_ARGS__)

/** @brief Logs a notice message to the server log (fmtlib style, binary form, auto-cached). */
#define ELOG_CACHE_NOTICE(fmt, ...) ELOG_CACHE(elog::ELEVEL_NOTICE, fmt, ##__VA_ARGS__)

/** @brief Logs a notice message to the server log (fmtlib style, binary form, pre-cached). */
#define ELOG_ID_NOTICE(cacheEntryId, ...) ELOG_ID(elog::ELEVEL_NOTICE, cacheEntryId, ##__VA_ARGS__)
#endif

/**
 * @brief Logs an informational message to the server log.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_INFO(fmt, ...) ELOG(elog::ELEVEL_INFO, fmt, ##__VA_ARGS__)

#ifdef ELOG_ENABLE_FMT_LIB
/** @brief Logs an informational message to the server log (fmtlib style). */
#define ELOG_FMT_INFO(fmt, ...) ELOG_FMT(elog::ELEVEL_INFO, fmt, ##__VA_ARGS__)

/** @brief Logs an informational message to the server log (fmtlib style, binary form). */
#define ELOG_BIN_INFO(fmt, ...) ELOG_BIN(elog::ELEVEL_INFO, fmt, ##__VA_ARGS__)

/**
 * @brief Logs an informational message to the server log (fmtlib style, binary form, auto-cached).
 */
#define ELOG_CACHE_INFO(fmt, ...) ELOG_CACHE(elog::ELEVEL_INFO, fmt, ##__VA_ARGS__)

/**
 * @brief Logs a informational message to the server log (fmtlib style, binary form, pre-cached).
 */
#define ELOG_ID_INFO(cacheEntryId, ...) ELOG_ID(elog::ELEVEL_INFO, cacheEntryId, ##__VA_ARGS__)
#endif

/**
 * @brief Logs a trace message to the server log.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_TRACE(fmt, ...) ELOG(elog::ELEVEL_TRACE, fmt, ##__VA_ARGS__)

#ifdef ELOG_ENABLE_FMT_LIB
/** @brief Logs a trace message to the server log (fmtlib style). */
#define ELOG_FMT_TRACE(fmt, ...) ELOG_FMT(elog::ELEVEL_TRACE, fmt, ##__VA_ARGS__)

/** @brief Logs a trace message to the server log (fmtlib style, binary form). */
#define ELOG_BIN_TRACE(fmt, ...) ELOG_BIN(elog::ELEVEL_TRACE, fmt, ##__VA_ARGS__)

/** @brief Logs a trace message to the server log (fmtlib style, binary form, auto-cached). */
#define ELOG_CACHE_TRACE(fmt, ...) ELOG_CACHE(elog::ELEVEL_TRACE, fmt, ##__VA_ARGS__)

/** @brief Logs a trace message to the server log (fmtlib style, binary form, pre-cached). */
#define ELOG_ID_TRACE(cacheEntryId, ...) ELOG_ID(elog::ELEVEL_TRACE, cacheEntryId, ##__VA_ARGS__)
#endif

/**
 * @brief Logs a debug message to the server log.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_DEBUG(fmt, ...) ELOG(elog::ELEVEL_DEBUG, fmt, ##__VA_ARGS__)

#ifdef ELOG_ENABLE_FMT_LIB
/** @brief Logs a debug message to the server log (fmtlib style). */
#define ELOG_FMT_DEBUG(fmt, ...) ELOG_FMT(elog::ELEVEL_DEBUG, fmt, ##__VA_ARGS__)

/** @brief Logs a debug message to the server log (fmtlib style, binary form). */
#define ELOG_BIN_DEBUG(fmt, ...) ELOG_BIN(elog::ELEVEL_DEBUG, fmt, ##__VA_ARGS__)

/** @brief Logs a debug message to the server log (fmtlib style, binary form, auto-cached). */
#define ELOG_CACHE_DEBUG(fmt, ...) ELOG_CACHE(elog::ELEVEL_DEBUG, fmt, ##__VA_ARGS__)

/** @brief Logs a debug message to the server log (fmtlib style, binary form, pre-cached). */
#define ELOG_ID_DEBUG(cacheEntryId, ...) ELOG_ID(elog::ELEVEL_DEBUG, cacheEntryId, ##__VA_ARGS__)
#endif

/**
 * @brief Logs a diagnostic message to the server log.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_DIAG(fmt, ...) ELOG(elog::ELEVEL_DIAG, fmt, ##__VA_ARGS__)

#ifdef ELOG_ENABLE_FMT_LIB
/** @brief Logs a diagnostic message to the server log (fmtlib style). */
#define ELOG_FMT_DIAG(fmt, ...) ELOG_FMT(elog::ELEVEL_DIAG, fmt, ##__VA_ARGS__)

/** @brief Logs a diagnostic message to the server log (fmtlib style, binary form). */
#define ELOG_BIN_DIAG(fmt, ...) ELOG_BIN(elog::ELEVEL_DIAG, fmt, ##__VA_ARGS__)

/** @brief Logs a diagnostic message to the server log (fmtlib style, binary form, auto-cached). */
#define ELOG_CACHE_DIAG(fmt, ...) ELOG_CACHE(elog::ELEVEL_DIAG, fmt, ##__VA_ARGS__)

/** @brief Logs a diagnostic message to the server log (fmtlib style, binary form, pre-cached). */
#define ELOG_ID_DIAG(cacheEntryId, ...) ELOG_ID(elog::ELEVEL_DIAG, cacheEntryId, ##__VA_ARGS__)
#endif

/**
 * @brief Begins a multi-part log message.
 * @param level The log level. If the log level is insufficient, then the message is dropped.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_BEGIN(level, fmt, ...)                          \
    {                                                        \
        elog::ELogLogger* logger = elog::getDefaultLogger(); \
        ELOG_BEGIN_EX(logger, level, fmt, ##__VA_ARGS__);    \
    }

/** @brief Begins a multi-part log message (fmtlib style). */
#ifdef ELOG_ENABLE_FMT_LIB
#define ELOG_FMT_BEGIN(level, fmt, ...)                       \
    {                                                         \
        elog::ELogLogger* logger = elog::getDefaultLogger();  \
        ELOG_FMT_BEGIN_EX(logger, level, fmt, ##__VA_ARGS__); \
    }
#endif

/**
 * @brief Appends formatted message to a multi-part log message.
 * @param fmt The message format.
 * @param ... The message arguments.
 */
#define ELOG_APPEND(fmt, ...)                                \
    {                                                        \
        elog::ELogLogger* logger = elog::getDefaultLogger(); \
        ELOG_APPEND_EX(logger, fmt, ##__VA_ARGS__);          \
    }

/** @brief Appends formatted message to a multi-part log message (fmtlib style). */
#ifdef ELOG_ENABLE_FMT_LIB
#define ELOG_FMT_APPEND(fmt, ...)                            \
    {                                                        \
        elog::ELogLogger* logger = elog::getDefaultLogger(); \
        ELOG_FMT_APPEND_EX(logger, fmt, ##__VA_ARGS__);      \
    }
#endif

/**
 * @brief Appends unformatted message to a multi-part log message.
 * @param msg The log message.
 */
#define ELOG_APPEND_NF(msg)                                  \
    {                                                        \
        elog::ELogLogger* logger = elog::getDefaultLogger(); \
        ELOG_APPEND_NF_EX(logger, msg);                      \
    }

/**
 * @brief Terminates a multi-part log message and writes it to the server log.
 */
#define ELOG_END()                                           \
    {                                                        \
        elog::ELogLogger* logger = elog::getDefaultLogger(); \
        ELOG_END_EX(logger);                                 \
    }

/**
 * @brief Logs a system error message to the server log.
 * @param syscall The system call that failed.
 * @param sysErr The system error code.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_SYS_ERROR_NUM(syscall, sysErr, fmt, ...)                       \
    {                                                                       \
        elog::ELogLogger* logger = elog::getDefaultLogger();                \
        ELOG_SYS_ERROR_NUM_EX(logger, syscall, sysErr, fmt, ##__VA_ARGS__); \
    }

/** @brief Logs a system error message to the server log (fmtlib style). */
#ifdef ELOG_ENABLE_FMT_LIB
#define ELOG_FMT_SYS_ERROR_NUM(syscall, sysErr, fmt, ...)                       \
    {                                                                           \
        elog::ELogLogger* logger = elog::getDefaultLogger();                    \
        ELOG_FMT_SYS_ERROR_NUM_EX(logger, syscall, sysErr, fmt, ##__VA_ARGS__); \
    }
#endif

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

/** @brief Logs a system error message to the server log (fmtlib style). */
#ifdef ELOG_ENABLE_FMT_LIB
#define ELOG_FMT_SYS_ERROR(syscall, fmt, ...)                        \
    {                                                                \
        int sysErr = errno;                                          \
        ELOG_FMT_SYS_ERROR_NUM(syscall, sysErr, fmt, ##__VA_ARGS__); \
    }
#endif

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
        elog::ELogLogger* logger = elog::getDefaultLogger();                  \
        ELOG_WIN32_ERROR_NUM_EX(logger, syscall, sysErr, fmt, ##__VA_ARGS__); \
    }

/** @brief Logs a system error message to the server log (fmtlib style). */
#ifdef ELOG_ENABLE_FMT_LIB
#define ELOG_FMT_WIN32_ERROR_NUM(syscall, sysErr, fmt, ...)                       \
    {                                                                             \
        elog::ELogLogger* logger = elog::getDefaultLogger();                      \
        ELOG_FMT_WIN32_ERROR_NUM_EX(logger, syscall, sysErr, fmt, ##__VA_ARGS__); \
    }
#endif

/**
 * @brief Logs a system error message to the server log.
 * @param syscall The system call that failed.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 * @note The error code for the system call is obtained through @ref errno. If you wish to provide
 * another error code then consider calling @ref ELOG_SYS_ERROR_NUM() instead.
 */
#define ELOG_WIN32_ERROR(syscall, fmt, ...)                        \
    {                                                              \
        DWORD sysErr = ::GetLastError();                           \
        ELOG_WIN32_ERROR_NUM(syscall, sysErr, fmt, ##__VA_ARGS__); \
    }

/** @brief Logs a system error message to the server log (fmtlib style). */
#ifdef ELOG_ENABLE_FMT_LIB
#define ELOG_FMT_WIN32_ERROR(syscall, fmt, ...)                        \
    {                                                                  \
        DWORD sysErr = ::GetLastError();                               \
        ELOG_FMT_WIN32_ERROR_NUM(syscall, sysErr, fmt, ##__VA_ARGS__); \
    }
#endif

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
#define ELOG_STACK_TRACE_EX(logger, level, title, skip, fmt, ...)     \
    {                                                                 \
        elog::ELogLogger* validLogger = elog::getValidLogger(logger); \
        if (logger->canLog(level)) {                                  \
            ELOG_EX(logger, level, fmt, ##__VA_ARGS__);               \
            elog::logStackTrace(logger, level, title, skip);          \
        }                                                             \
    }

/** @brief Logs the stack trace of the current thread (fmtlib style). */
#ifdef ELOG_ENABLE_FMT_LIB
#define ELOG_FMT_STACK_TRACE_EX(logger, level, title, skip, fmt, ...) \
    {                                                                 \
        elog::ELogLogger* validLogger = elog::getValidLogger(logger); \
        if (logger->canLog(level)) {                                  \
            ELOG_FMT_EX(logger, level, fmt, ##__VA_ARGS__);           \
            elog::logStackTrace(logger, level, title, skip);          \
        }                                                             \
    }
#endif

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
    {                                                                 \
        elog::ELogLogger* validLogger = elog::getValidLogger(logger); \
        if (logger->canLog(level)) {                                  \
            ELOG_EX(logger, level, fmt, ##__VA_ARGS__);               \
            elog::logAppStackTrace(logger, level, title, skip);       \
        }                                                             \
    }

/** @brief Logs the stack trace of all running threads in the application (fmtlib style). */
#ifdef ELOG_ENABLE_FMT_LIB
#define ELOG_FMT_APP_STACK_TRACE_EX(logger, level, title, skip, fmt, ...) \
    {                                                                     \
        elog::ELogLogger* validLogger = elog::getValidLogger(logger);     \
        if (logger->canLog(level)) {                                      \
            ELOG_FMT_EX(logger, level, fmt, ##__VA_ARGS__);               \
            elog::logAppStackTrace(logger, level, title, skip);           \
        }                                                                 \
    }
#endif

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
        elog::ELogLogger* logger = elog::getDefaultLogger();                 \
        ELOG_STACK_TRACE_EX(logger, level, title, skip, fmt, ##__VA_ARGS__); \
    }

/** @brief Logs the stack trace of the current thread, using default logger (fmtlib style). */
#ifdef ELOG_ENABLE_FMT_LIB
#define ELOG_FMT_STACK_TRACE(level, title, skip, fmt, ...)                       \
    {                                                                            \
        elog::ELogLogger* logger = elog::getDefaultLogger();                     \
        ELOG_FMT_STACK_TRACE_EX(logger, level, title, skip, fmt, ##__VA_ARGS__); \
    }
#endif

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
        elog::ELogLogger* logger = elog::getDefaultLogger();                     \
        ELOG_APP_STACK_TRACE_EX(logger, level, title, skip, fmt, ##__VA_ARGS__); \
    }

/**
 * @brief Logs the stack trace of all running threads in the application, using default logger
 * (fmtlib style).
 */
#ifdef ELOG_ENABLE_FMT_LIB
#define ELOG_FMT_APP_STACK_TRACE(level, title, skip, fmt, ...)                       \
    {                                                                                \
        elog::ELogLogger* logger = elog::getDefaultLogger();                         \
        ELOG_FMT_APP_STACK_TRACE_EX(logger, level, title, skip, fmt, ##__VA_ARGS__); \
    }
#endif

#endif  // ELOG_ENABLE_STACK_TRACE

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