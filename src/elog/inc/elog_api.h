#ifndef __ELOG_API_H__
#define __ELOG_API_H__

#include "elog_config.h"
#include "elog_filter.h"
#include "elog_formatter.h"
#include "elog_level.h"
#include "elog_logger.h"
#include "elog_params.h"
#include "elog_props.h"
#include "elog_rate_limiter.h"
#include "elog_report_handler.h"
#include "elog_schema_handler.h"
#include "elog_source.h"
#include "elog_target.h"

#ifdef ELOG_ENABLE_STACK_TRACE
#include "dbg_stack_trace.h"
#endif

#ifdef ELOG_ENABLE_LIFE_SIGN
#include "os_thread_manager.h"
#endif

// include fmtlib main header
#ifdef ELOG_ENABLE_FMT_LIB
#include "elog_fmt_lib.h"
#endif

#ifdef ELOG_ENABLE_CONFIG_SERVICE
#include "cfg_srv/elog_config_service_publisher.h"
#endif

/** @file The elog module facade. */

namespace elog {

/**************************************************************************************
 *
 *                          Initialization/Termination Interface
 *
 **************************************************************************************/

/**
 * @brief Initializes the ELog library.
 * @param params Initialization parameters.
 * @return true If succeeded, otherwise false.
 */
extern ELOG_API bool initialize(const ELogParams& params = ELogParams());

/** @brief Releases all resources allocated for the ELogSystem. */
extern ELOG_API void terminate();

/** @brief Queries whether the ELog library is initialized. */
extern ELOG_API bool isInitialized();

/**************************************************************************************
 *
 *                          Reload Configuration Interface
 *
 **************************************************************************************/

#ifdef ELOG_ENABLE_RELOAD_CONFIG
/**
 * @brief Reloads configuration from a file. If no configuration file path is specified, then the
 * path provided during @ref initialize() is used.
 *
 * @note Only log levels are reloaded. All other configuration items are ignored.
 */
extern ELOG_API bool reloadConfigFile(const char* configFile = nullptr);

/**
 * @brief Reloads configuration from a string.
 * @note Only log levels are reloaded. All other configuration items are ignored.
 */
extern ELOG_API bool reloadConfigStr(const char* configStr);

/**
 * @brief Sets the configuration file used for periodic configuration reload.
 *
 * @note Setting a null or empty path stops periodic reload if it was active. Setting a valid path
 * either starts periodic reloading (only if reload period is set as well) or updates the  file
 * change check period.
 *
 * @param configFilePath The configuration file path.
 * @return The operation's result.
 */
extern ELOG_API bool setPeriodicReloadConfigFile(const char* configFilePath);

/**
 * @brief Set the configuration reload period in milliseconds.
 *
 * @note Setting a value of zero stops periodic reload if it was active. Setting a positive value
 * either starts periodic reloading (only if configuration file path is set as well) or updates the
 * the file change check period.
 *
 * @param reloadPeriodMillis The configuration reload period in milliseconds.
 * @return The operation's result.
 */
extern ELOG_API bool setReloadConfigPeriodMillis(uint64_t reloadPeriodMillis);
#endif

/**************************************************************************************
 *
 *                     Remote Configuration Service Interface
 *
 **************************************************************************************/

#ifdef ELOG_ENABLE_CONFIG_SERVICE
/**
 * @brief Enables the remote configuration service. If it is already enabled nothing happens.
 * Note that explicit call to start the remote configuration service is still required.
 */
extern ELOG_API bool enableConfigService();

/**
 * @brief Disables the remote configuration service. If it is running, then it will be stopped. If
 * it is already disabled nothing happens.
 */
extern ELOG_API bool disableConfigService();

/** @brief Starts the configuration service. If it is already started nothing happens. */
extern ELOG_API bool startConfigService();

/** @brief Stops the configuration service. If it is already stopped nothing happens. */
extern ELOG_API bool stopConfigService();

/**
 * @brief Restarts the configuration service. If it is already stopped, it will only be started.
 * @note Any configuration changes made to the configuration service up until to this point will be
 * used.
 */
extern ELOG_API bool restartConfigService();

/**
 * @brief Sets the remote configuration service details.
 *
 * @param host The host name or address.
 * @param port The listening port.
 * @param restartConfigService Optionally specifies whether the configuration service should be
 * restarted so the change may take effect.
 * @return The operation's result.
 *
 * @note The remote configuration service needs to be restarted for this changes to take effect.
 *
 * @see also @ref ELogConfigServiceParams::m_configServiceHost.
 * @see also @ref ELogConfigServiceParams::m_configServicePort.
 */
extern ELOG_API bool setConfigServiceDetails(const char* host, int port,
                                             bool restartConfigService = false);

/**
 * @brief Enables the remote configuration service publisher. Restarts the configuration services
 * if ordered to, and updates the publisher in use.
 * @note If not ordered to restart, then the changes will take place only the next time the service
 * is restarted.
 * @note If the publisher was disabled before the call was made, and the currently installed
 * publisher is not null, then it will be handed over to the configuration service for periodic
 * service publishing. If configured to restart the changes will take effect immediately, otherwise
 * they will take effect during the next time the configuration service is started. In any other
 * case (except for a possible restart), nothing will happen.
 */
extern ELOG_API bool enableConfigServicePublisher(bool restartConfigService = false);

/** @brief Disables the remote configuration service publisher. Restarts the configuration services
 * if ordered to, and updates the publisher in use.
 * @note If not ordered to restart, then the changes will take place only the next time the service
 * is restarted.
 * @note If the publisher was enabled before the call was made, and the currently installed
 * publisher is not null, then a null publisher will be handed over to the configuration service so
 * that periodic service publishing will not take place. If configured to restart the changes will
 * take effect immediately, otherwise they will take effect during the next time the configuration
 * service is started. In any other case (except for a possible restart), nothing will happen.
 */
extern ELOG_API bool disableConfigServicePublisher(bool restartConfigService = false);

/**
 * @brief Sets the configuration service publisher. May trigger a restart of the remote
 * configuration service.
 *
 * @note Caller is responsible for managing the life-cycle of the publisher objects. Also the
 * publisher must be already initialized (i.e. a successful call to
 * ElogConfigServicePublisher::initialize() must have already taken place before this call is made).
 *
 * @param publisher The publisher to set. Could be null in order to disable publishing altogether.
 * @param restartConfigService Optionally specifies whether the configuration service should be
 * restarted so the change may take effect.
 * @return The operation's result.
 *
 * @note The remote configuration service needs to be restarted for this change to take effect.
 */
extern ELOG_API bool setConfigServicePublisher(ELogConfigServicePublisher* publisher,
                                               bool restartConfigService = false);
#endif

/**************************************************************************************
 *
 *                          Pre-Init Log Queueing Interface
 *
 **************************************************************************************/

/**
 * @brief Retrieves the logger that is used to accumulate log messages while the ELog library
 * has not initialized yet.
 */
extern ELOG_API ELogLogger* getPreInitLogger();

/** @brief Queries whether there are any accumulated log messages. */
extern ELOG_API bool hasAccumulatedLogMessages();

/**
 * @brief Retrieves the number of accumulated log messages. Optional filter may be used to avoid
 * counting certain message types.
 */
extern ELOG_API uint32_t getAccumulatedMessageCount(ELogFilter* filter = nullptr);

/**
 * @brief Discards all accumulated log messages. This will prevent from log targets added in
 * the future to receive all log messages that were accumulated before the ELog library was
 * initialized.
 */
extern ELOG_API void discardAccumulatedLogMessages();

/**************************************************************************************
 *
 *                          Internal Reporting Interface
 *
 **************************************************************************************/

/** @brief Installs a handler for ELog's internal log message reporting. */
extern ELOG_API void setReportHandler(ELogReportHandler* reportHandler);

/** @brief Configures the log level of ELog's internal log message reports. */
extern ELOG_API void setReportLevel(ELogLevel reportLevel);

/** @brief Retrieves the log level of ELog's internal log message reports. */
extern ELOG_API ELogLevel getReportLevel();

/** @brief Registers a schema handler by name. */
extern ELOG_API bool registerSchemaHandler(const char* schemeName,
                                           ELogSchemaHandler* schemaHandler);

/**************************************************************************************
 *
 *                          Lazy Time Source Interface
 *
 **************************************************************************************/

/** @brief Enables the lazy time source. */
extern ELOG_API void enableLazyTimeSource();

/** @brief Disables the lazy time source. */
extern ELOG_API void disableLazyTimeSource();

/**
 * @brief Configures the lazy time source.
 * @param resolution The clock resolution of the lazy time source.
 * @param resolutionUnits The resolution units of the lazy time source.
 */
extern ELOG_API void configureLazyTimeSource(uint64_t resolution, ELogTimeUnits resolutionUnits);

/**************************************************************************************
 *
 *                              Life-Sign Interface
 *
 **************************************************************************************/

#ifdef ELOG_ENABLE_LIFE_SIGN
/**
 * @brief Sets up life-sign reports. This call is thread-safe. This calls allows configuring
 * life-sign reports to be automatically sent by ELog to a shared memory segment. Such configuration
 * is per-log level, and allows specifying the frequency of the reports (i.e. once in every N calls,
 * or imposing some rate limit). The following report scopes are supported:
 *
 * - application scope
 * - thread scope
 * - log-source scope
 *
 * When application scope is specified, each logging call is checked for life-sign report. When
 * frequent logging is used, this may incur a slight performance hit.
 *
 * When thread-scope is specified, then only logging of the current thread (or target thread
 * specified by name), are affected.
 *
 * When log-source scope is specified, then only logging calls made through loggers of the specified
 * log source are affected.
 *
 * @note This function may be called several times for specific threads or log sources.
 *
 * @param scope Report scope. If scope is application then the name parameter is ignored. If the
 * scope is log-source, then the qualified name of the log source should also be specified. If the
 * scope is thread then a name can be optionally specified (see below).
 * @param level The log level for which the report is being set up.
 * @param frequencySpec The report frequency.
 * @param name The name of the log source or target thread. In case of log source this is expected
 * to be a fully qualified name (see @ref ELogSource). In case of thread, this must match a thread,
 * whose name was previously set with a call to @ref setCurrentThreadName(). If the scope is thread
 * and no thread name was specified (i.e. null or empty string), then life-sign report is configured
 * for the current thread.
 * @param isRegex Specifies whether the specified name should be treated as a regular expression
 * matching several threads or log sources. This allows addressing several threads or log sources in
 * a single call. This parameter is ignored when setting life-sign report for the entire application
 * or for the curren thread.
 * @return The operation's result.
 */
extern ELOG_API bool setLifeSignReport(ELogLifeSignScope scope, ELogLevel level,
                                       const ELogFrequencySpec& frequencySpec,
                                       const char* name = nullptr, bool isRegex = false);

/**
 * @brief Removes life-sign periodic reports. This call is thread-safe.
 * @param scope Report scope. If scope is application then the name parameter is ignored. If the
 * scope is log-source, then a name should also be specified. If the scope is thread then a name can
 * be optionally specified (see below).
 * @param level The log level for which the report is to be removed.
 * @param name The name of the log source or target thread. In case of log source this is expected
 * to be a fully qualified name (see @ref ELogSource). In case of thread, this must match a thread,
 * whose name was previously set with a call to @ref setCurrentThreadName(). If the scope is thread
 * and no thread name was specified, then life-sign report is removed for the current thread.
 * @param isRegex Specifies whether the specified name should be treated as a regular expression
 * matching several threads or log sources. This allows addressing several threads or log sources in
 * a single call.
 * @return The operation's result.
 */
extern ELOG_API bool removeLifeSignReport(ELogLifeSignScope scope, ELogLevel level,
                                          const char* name = nullptr, bool isRegex = false);

/**
 * @brief Set life-sign report for the specified log source. For more details see @ref
 * setLifeSignReport().
 * @param level The log level for which the report is being set up.
 * @param frequencySpec The report frequency.
 * @param logSource The target log source.
 * @return The operation's result.
 */
extern ELOG_API bool setLogSourceLifeSignReport(ELogLevel level,
                                                const ELogFrequencySpec& frequencySpec,
                                                ELogSource* logSource);

/**
 * @brief Remove life-sign report for the specified log source. For more details see @ref
 * removeLifeSignReport().
 * @param level The log level for which the report is being set up.
 * @param logSource The target log source.
 * @return The operation's result.
 */
extern ELOG_API bool removeLogSourceLifeSignReport(ELogLevel level, ELogSource* logSource);

/** @brief Configures log line format for life sign reports. */
extern ELOG_API bool setLifeSignLogFormat(const char* logFormat);

/**
 * @brief Sets the life-sign synchronization period in milliseconds. Since life-sign reports are
 * written to shared memory, on Windows platforms it is required occasionally to synchronize shared
 * memory segment contents to disk. This utility function allows setting a periodic timer for this
 * purpose. A manual synchronization function exists as well (see @ref syncLifeSignReport()).
 *
 * @note This should be called once during application startup phase, but it is safe to call it
 * several times during the life-time of the application.
 *
 * @param syncPeriodMillis The synchronization period in milliseconds. Setting this value to zero
 * would cause periodic synchronization to stop.
 */
extern ELOG_API void setLifeSignSyncPeriod(uint64_t syncPeriodMillis);

/** @brief Synchronizes the life-sign report shared memory segment to disk (Windows only). */
extern ELOG_API bool syncLifeSignReport();

/** @brief Voluntarily send a life sign report. */
extern ELOG_API void reportLifeSign(const char* msg);

/**
 * @brief Configures life sign report by a configuration string. The expected format is as follows:
 *
 * scope:log-level:freq-spec:optional-name
 *
 * scope is anyone of: app, thread, log_source
 * freq-spec is either of the form 'every[N]', specifying one message per N messages (to be sent to
 * life-sign report), or rate_limit[msx-msg:timeout:unit], specifying rate limit. When scope is
 * thread or log_source, a name is expected, designating the name of the thread or the log source.
 * Following are some examples:
 *
 * app:ERROR:every[1]
 * app:WARN:every[10],
 * thread:WARN:every[1]:monitor_thread
 * log_source:INFO:rate[3:2:second]:file_manager
 *
 * @param lifeSignCfg The configuration string.
 * @return The operation's result.
 */
extern ELOG_API bool configureLifeSign(const char* lifeSignCfg);

/**
 * @brief Installs a notifier for the current thread so that incoming signals can be processed
 * (mostly required on Windows). This is required for configuring life sign reports on a target
 * thread by name, mostly on Windows platforms (but not only), since this operation may deadlock.
 * The notifier should wake up the target thread so that it can process incoming signals (or APC on
 * Windows platforms). Consequently the target thread can return to sleep/wait.
 *
 * @note @ref setCurrentThreadName() must have been called for the current thread prior to this
 * call.
 *
 * @return True if operation succeeded, otherwise false, indicating the name of the current thread
 * has not been set yet.
 */
extern ELOG_API bool setCurrentThreadNotifier(dbgutil::ThreadNotifier* notifier);

/**
 * @brief Installs a notifier for the named thread, so that incoming signals can be processed
 * (mostly required on Windows). This is required for configuring life sign reports on a target
 * thread by name, mostly on Windows platforms (but not only), since this operation may deadlock.
 * The notifier should wake up the target thread so that it can process incoming signals (or APC on
 * Windows platforms). Consequently the target thread can return to sleep/wait.
 *
 * @note @ref setCurrentThreadName() must have been called for the some thread prior to this call.
 *
 * @return True if operation succeeded, otherwise false, indicating no thread with the given name
 * was found.
 */
extern ELOG_API bool setThreadNotifier(const char* threadName, dbgutil::ThreadNotifier* notifier);
#endif

/**************************************************************************************
 *
 *                              Configuration Interface
 *
 **************************************************************************************/

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
 * @param configPath The properties configuration file path.
 * @param defineLogSources Optional parameter specifying whether each log source configuration item
 * triggers creation of the log source.
 * @param defineMissingPath In case @ref defineLogSources is true, then optionally define all
 * missing loggers along the name path. If not specified, and a logger on the path from root to leaf
 * is missing, then the call fails.
 * @return true If configuration succeeded, otherwise false.
 */
extern ELOG_API bool configureByPropFile(const char* configPath, bool defineLogSources = true,
                                         bool defineMissingPath = true);

/**
 * @brief Configures the ELog System from a properties map.
 * The following properties are recognized:
 * - log_format: log line format specification. See @ref configureLogFormat() for more details.
 * - log_level: expected value is any log level string (without the "ELEVEL_" prefix).
 *   Determines the global (root source) log level.
 * - <qualified-source-name>.log_level: Log level of a log source.
 * - log_target: expected log target URL.
 * @param props The properties map.
 * @param defineLogSources Optional parameter specifying whether each log source configuration item
 * triggers creation of the log source.
 * @param defineMissingPath In case @ref defineLogSources is true, then optionally define all
 * missing loggers along the name path. If not specified, and a logger on the path from root to leaf
 * is missing, then the call fails.
 * @return true If configuration succeeded, otherwise false.
 */
extern ELOG_API bool configureByProps(const ELogPropertySequence& props,
                                      bool defineLogSources = true, bool defineMissingPath = true);

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
 * @param configPath The properties configuration file path.
 * @param defineLogSources Optional parameter specifying whether each log source configuration item
 * triggers creation of the log source.
 * @param defineMissingPath In case @ref defineLogSources is true, then optionally define all
 * missing loggers along the name path. If not specified, and a logger on the path from root to leaf
 * is missing, then the call fails.
 * @return true If configuration succeeded, otherwise false.
 */
extern ELOG_API bool configureByPropFileEx(const char* configPath, bool defineLogSources = true,
                                           bool defineMissingPath = true);

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
 * @param defineLogSources Optional parameter specifying whether each log source configuration item
 * triggers creation of the log source.
 * @param defineMissingPath In case @ref defineLogSources is true, then optionally define all
 * missing loggers along the name path. If not specified, and a logger on the path from root to leaf
 * is missing, then the call fails.
 * @return true If configuration succeeded, otherwise false.
 */
extern ELOG_API bool configureByPropsEx(const ELogPropertyPosSequence& props,
                                        bool defineLogSources = true,
                                        bool defineMissingPath = true);

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
 * @param defineLogSources Optional parameter specifying whether each log source configuration item
 * triggers creation of the log source.
 * @param defineMissingPath In case @ref defineLogSources is true, then optionally define all
 * missing loggers along the name path. If not specified, and a logger on the path from root to leaf
 * is missing, then the call fails.
 * @return true If configuration succeeded, otherwise false.
 */
extern ELOG_API bool configureByFile(const char* configPath, bool defineLogSources = true,
                                     bool defineMissingPath = true);

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
 * @param defineLogSources Optional parameter specifying whether each log source configuration item
 * triggers creation of the log source.
 * @param defineMissingPath In case @ref defineLogSources is true, then optionally define all
 * missing loggers along the name path. If not specified, and a logger on the path from root to leaf
 * is missing, then the call fails.
 * @return true If configuration succeeded, otherwise false.
 */
extern ELOG_API bool configureByStr(const char* configStr, bool defineLogSources = true,
                                    bool defineMissingPath = true);

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
 * @param defineLogSources Optional parameter specifying whether each log source configuration item
 * triggers creation of the log source.
 * @param defineMissingPath In case @ref defineLogSources is true, then optionally define all
 * missing loggers along the name path. If not specified, and a logger on the path from root to leaf
 * is missing, then the call fails.
 * @return true If configuration succeeded, otherwise false.
 */
extern ELOG_API bool configure(ELogConfig* config, bool defineLogSources = true,
                               bool defineMissingPath = true);

/**************************************************************************************
 *
 *                        Log Target Management Interface
 *
 **************************************************************************************/

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
extern ELOG_API ELogTargetId configureLogTarget(const char* logTargetCfg);

/**
 * @brief Adds a file log target, optionally buffered, segmented or rotating.
 * @param logFilePath The log file path (including lgo file name).
 * @param bufferSize Optional buffer size for file buffering. Specify zero to disable buffering.
 * @param useLock (Relevant only for buffered logging) Optionally specify use of lock. By
 * default none is used. If it is known that logging will take place in a thread-safe manner
 * (e.g. behind a global lock), then there is no need for the file target to make use of its own
 * lock. Pay attention that when buffering is used in a multi-threaded scenario, using a lock is
 * mandatory, and without a lock behavior is undefined.
 * @param segmentLimitMB Optionally specify a segment size limit, which will cause the log file
 * to be divided into segments. That is, when log file exceeds this limit, the log file segment
 * is closed, and a new log file segment is created.
 * @param segmentCount Optionally specify segment count limit (relevant only when a segment size
 * limit is specified). The effect of this would be a rotating log file, such that when the
 * amount of log segments has been used up, the first log segment is begin reused (thus
 * discarding old segments' log data).
 * @param enableStats Specifies whether log target statistics should be collected.
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
                                              uint32_t segmentCount = 0, bool enableStats = true,
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
 * @param enableStats Specifies whether log target statistics should be collected.
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
                                                 bool enableStats = true,
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
 * @brief Adds a dedicated tracer, that receives messages only from a specific logger and directs
 * all logs only to a specified log target.
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

/**************************************************************************************
 *
 *                        Log Source Management Interface
 *
 **************************************************************************************/

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
                                            bool defineMissingPath = true);

/** @brief Retrieves a log source by its qualified name. Returns null if not found. */
extern ELOG_API ELogSource* getLogSource(const char* qualifiedName);

/** @brief Retrieves a log source by its id. Returns null if not found. */
extern ELOG_API ELogSource* getLogSource(ELogSourceId logSourceId);

/** @brief Retrieves the root log source. */
extern ELOG_API ELogSource* getRootLogSource();

/**************************************************************************************
 *
 *                           Logger Access Interface
 *
 **************************************************************************************/

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
 * @param qualifiedSourceName The qualified log source name, from which a logger is to be obtained.
 * @param defineLogSource Optionally orders to define the log source, in case it is not defined.
 * @param defineMissingPath In case @ref defineLogSource is true, then optionally define
 * all missing loggers along the name path. If not specified, and a logger on the path from root
 * to leaf is missing, then the call fails.
 * @note This call is NOT thread safe.
 */
extern ELOG_API ELogLogger* getPrivateLogger(const char* qualifiedSourceName,
                                             bool defineLogSource = true,
                                             bool defineMissingPath = true);

/**
 * @brief Retrieves a shared (can be used by more than one thread) logger from a log source by
 * its qualified name. The logger is managed and should not be deleted by the caller.
 * @param qualifiedSourceName The qualified log source name, from which a logger is to be obtained.
 * @param defineLogSource Optionally orders to define the log source, in case it is not defined.
 * @param defineMissingPath In case @ref defineLogSource is true, then optionally define
 * all missing loggers along the name path. If not specified, and a logger on the path from root
 * to leaf is missing, then the call fails.
 * @note This call is NOT thread safe.
 */
extern ELOG_API ELogLogger* getSharedLogger(const char* qualifiedSourceName,
                                            bool defineLogSource = true,
                                            bool defineMissingPath = true);

/**************************************************************************************
 *
 *                              Log Level Interface
 *
 **************************************************************************************/

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

/**************************************************************************************
 *
 *                              Log Formatting Interface
 *
 **************************************************************************************/

/**
 * @brief Configures the format of log lines.
 * @note The log line format specification is a string with normal text and white space, that
 * may contain special token references. The following special tokens are in use:
 * ${rid} - the log record id.
 * ${time} - the loging time (local time with milliseconds).
 * ${time_epoch} - the logging time (unix epoch milliseconds)
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

/**************************************************************************************
 *
 *                      Format Message Caching Interface
 *
 **************************************************************************************/

/**
 * @brief Caches a format message.
 *
 * @param fmt The format message to cache
 * @return The format messages's cache entry id, or @ref ELOG_INVALID_CACHE_ENTRY_ID if failed.
 */
extern ELOG_API ELogCacheEntryId cacheFormatMsg(const char* fmt);

/**
 * @brief Retrieves a previously cached format message.
 *
 * @param entryId The format messages' cache entry id.
 * @return The cached format message or null if not found.
 */
extern ELOG_API const char* getCachedFormatMsg(ELogCacheEntryId entryId);

/**
 * @brief Retrieves a previously cached format message, or caches it if not found
 *
 * @param fmt The format message whose cache entry id is to be retrieved. If not found, then the
 * format message is cached, and the new cache entry id is returned.
 * @return The format messages's cache entry id, or @ref ELOG_INVALID_CACHE_ENTRY_ID if failed.
 */
extern ELOG_API ELogCacheEntryId getOrCacheFormatMsg(const char* fmt);

/**************************************************************************************
 *
 *                  User-controlled Field Selector Configuration
 *
 **************************************************************************************/

/** @brief Sets the application's name, to be referenced by token ${app}. */
extern ELOG_API void setAppName(const char* appName);

/**
 * @brief Retrieves the installed application's name. Returns empty string (not null) if none was
 * installed prior to the call.
 */
extern ELOG_API const char* getAppName();

/**
 * @brief Sets the current thread's name, to be referenced by token ${tname}.
 * @param threadName The name of the thread. Duplicate names are not allowed.
 * @return True if succeeded, or false if name is already used by another thread.
 */
extern ELOG_API bool setCurrentThreadName(const char* threadName);

/**
 * @brief Retrieves the installed current thread's name. Returns empty string (not null) if none was
 * installed prior to the call.
 */
extern ELOG_API const char* getCurrentThreadName();

/**************************************************************************************
 *
 *                              Log Filtering Interface
 *
 **************************************************************************************/

/** @brief Configures top-level log filter form configuration string. */
extern ELOG_API bool configureLogFilter(const char* logFilterCfg);

/** @brief Installs a custom log filter. */
extern ELOG_API void setLogFilter(ELogFilter* logFilter);

/** @brief Configures top-level rate limiter form configuration string. */
extern ELOG_API bool configureRateLimit(const char* rateLimitCfg, bool replaceGlobalFilter = true);

/**
 * @brief Sets a global rate limit on message logging.
 * @param maxMsg The maximum number of messages that can be logged in a time interval.
 * @param timeout The rate limit timeout interval value.
 * @param timeoutUnits The rate limit timeout units.
 * @param replaceGlobalFilter Specified what to do in case of an existing global log filter. If
 * set to true, then the rate limiter will replace any configured global log filter. If set to
 * false then the rate limiter will be combined with the currently configured global log filter
 * using OR operator. In no global filter is currently being used then this parameter has no
 * significance and is ignored.
 * @return True if the operation succeeded, otherwise false.
 * @note Setting the global rate limit will replace the current global log filter. If the
 * intention is to add a rate limiting to the current log filter
 */
extern ELOG_API bool setRateLimit(uint64_t maxMsg, uint64_t timeout, ELogTimeUnits timeoutUnits,
                                  bool replaceGlobalFilter = true);

/**
 * @brief Filters a log record.
 * @param logRecord The log record to filter.
 * @return true If the log record is to be processed.
 * @return false If the log record is to be discarded.
 */
extern ELOG_API bool filterLogMsg(const ELogRecord& logRecord);

/**************************************************************************************
 *
 *                          Stack Trace Logging Interface
 *
 **************************************************************************************/

#ifdef ELOG_ENABLE_STACK_TRACE
/**
 * @brief Prints stack trace to log with the given log level.
 * @param logger The logger to use for printing the stack trace.
 * @param logLevel Optional log level.
 * @param title Optional title to print before each thread stack trace.
 * @param skip Optionally specifies the number of frames to skip.
 * @param formatter Optional stack entry formatter. Pass null to use default formatting.
 */
extern ELOG_API void logStackTrace(ELogLogger* logger, ELogLevel logLevel = ELEVEL_INFO,
                                   const char* title = "", int skip = 0,
                                   dbgutil::StackEntryFormatter* formatter = nullptr);

/**
 * @brief Prints stack trace to log with the given log level. Context is either captured by
 * calling thread, or is passed by OS through an exception/signal handler.
 * @param logger The logger to use for printing the stack trace.
 * @param context Optional OS-specific thread context. Pass null to log current thread call stack.
 * @param logLevel Optional log level.
 * @param title Optional title to print before each thread stack trace.
 * @param skip Optionally specifies the number of frames to skip.
 * @param formatter Optional stack entry formatter. Pass null to use default formatting.
 */
extern ELOG_API void logStackTraceContext(ELogLogger* logger, void* context = nullptr,
                                          ELogLevel logLevel = ELEVEL_INFO, const char* title = "",
                                          int skip = 0,
                                          dbgutil::StackEntryFormatter* formatter = nullptr);

/**
 * @brief Prints stack trace of all running threads to log with the given log level.
 * @param logger The logger to use for printing the stack trace.
 * @param logLevel Optional log level.
 * @param title Optional title to print before each thread stack trace.
 * @param skip Optionally specifies the number of frames to skip.
 * @param formatter Optional stack entry formatter. Pass null to use default formatting.
 */
extern ELOG_API void logAppStackTrace(ELogLogger* logger, ELogLevel logLevel = ELEVEL_INFO,
                                      const char* title = "", int skip = 0,
                                      dbgutil::StackEntryFormatter* formatter = nullptr);
#endif

/**************************************************************************************
 *
 *                          System Error Logging Interface
 *
 **************************************************************************************/

/** @brief Converts system error code to string. */
extern ELOG_API char* sysErrorToStr(int sysErrorCode);

#ifdef ELOG_WINDOWS
/** @brief Converts Windows system error code to string. */
extern ELOG_API char* win32SysErrorToStr(unsigned long sysErrorCode);

/** @brief Deallocates Windows system error string. */
extern ELOG_API void win32FreeErrorStr(char* errStr);
#endif

/**************************************************************************************
 *                       Logging Macros Helper Functions/Classes
 **************************************************************************************/

/** @brief Retrieves any valid logger (helper function for logging macros). */
inline ELogLogger* getValidLogger(ELogLogger* logger) {
    if (logger != nullptr) {
        return logger;
    } else if (elog::isInitialized()) {
        return elog::getDefaultLogger();
    } else {
        return elog::getPreInitLogger();
    }
}

/** @brief Helper class for implementing "once" macros. */
class ELOG_API ELogOnce {
public:
    ELogOnce() : m_once(false) {}
    ELogOnce(const ELogOnce&) = delete;
    ELogOnce(ELogOnce&&) = delete;
    ELogOnce& operator=(ELogOnce&) = delete;
    ~ELogOnce() {}

    inline operator bool() {
        bool onceValue = m_once.load(std::memory_order_acquire);
        return (onceValue == false &&
                m_once.compare_exchange_strong(onceValue, true, std::memory_order_seq_cst));
    }

private:
    std::atomic<bool> m_once;
};

/** @brief Helper class for implementing "moderate" macros. */
class ELOG_API ELogModerate {
public:
    ELogModerate(const char* fmt, uint64_t maxMsgs, uint64_t timeout, ELogTimeUnits units)
        : m_fmt(fmt),
          m_rateLimiter(maxMsgs, timeout, units),
          m_discardCount(0),
          m_isDiscarding(false),
          m_startDiscardCount(0) {}
    ELogModerate(const ELogModerate&) = delete;
    ELogModerate(ELogModerate&&) = delete;
    ELogModerate& operator=(ELogModerate&) = delete;
    ~ELogModerate() {}

    bool moderate();

private:
    const char* m_fmt;
    ELogRateLimiter m_rateLimiter;
    static ELogRecord m_dummy;
    std::atomic<uint64_t> m_discardCount;
    std::atomic<bool> m_isDiscarding;
    std::chrono::steady_clock::time_point m_startDiscardTime;
    uint64_t m_startDiscardCount;
};

/**************************************************************************************
 *                          Logging Statistics
 **************************************************************************************/

/** @brief Enables log statistics collection (per-level counters). */
extern ELOG_API void enableLogStatistics();

/** @brief Disables log statistics collection (per-level counters). */
extern ELOG_API void disableLogStatistics();

/** @brief Retrieves the per-level message count statistics (global scope). */
extern ELOG_API void getLogStatistics(ELogStatistics& stats);

extern ELOG_API void resetLogStatistics();

}  // namespace elog

/**************************************************************************************
 *
 *                              Logging Macros
 *
 **************************************************************************************/

#define ELOG_BASE(logger, level, fmt, ...) \
    logger->logFormat(level, __FILE__, __LINE__, ELOG_FUNCTION, fmt, ##__VA_ARGS__)

/**
 * @brief Logs a formatted message.
 * @param logger The logger used for message formatting.
 * @param level The log level. If the log level is insufficient, then the message is dropped.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_EX(logger, level, fmt, ...)                              \
    {                                                                 \
        elog::ELogLogger* validLogger = elog::getValidLogger(logger); \
        if (validLogger->canLog(level)) {                             \
            ELOG_BASE(validLogger, level, fmt, ##__VA_ARGS__);        \
        }                                                             \
    }

// per-level macros
#define ELOG_FATAL_EX(logger, fmt, ...) ELOG_EX(logger, elog::ELEVEL_FATAL, fmt, ##__VA_ARGS__)
#define ELOG_ERROR_EX(logger, fmt, ...) ELOG_EX(logger, elog::ELEVEL_ERROR, fmt, ##__VA_ARGS__)
#define ELOG_WARN_EX(logger, fmt, ...) ELOG_EX(logger, elog::ELEVEL_WARN, fmt, ##__VA_ARGS__)
#define ELOG_NOTICE_EX(logger, fmt, ...) ELOG_EX(logger, elog::ELEVEL_NOTICE, fmt, ##__VA_ARGS__)
#define ELOG_INFO_EX(logger, fmt, ...) ELOG_EX(logger, elog::ELEVEL_INFO, fmt, ##__VA_ARGS__)
#define ELOG_TRACE_EX(logger, fmt, ...) ELOG_EX(logger, elog::ELEVEL_TRACE, fmt, ##__VA_ARGS__)
#define ELOG_DEBUG_EX(logger, fmt, ...) ELOG_EX(logger, elog::ELEVEL_DEBUG, fmt, ##__VA_ARGS__)
#define ELOG_DIAG_EX(logger, fmt, ...) ELOG_EX(logger, elog::ELEVEL_DIAG, fmt, ##__VA_ARGS__)

/** @brief Log formatted message (no logger). */
#define ELOG(level, fmt, ...) ELOG_EX(nullptr, level, fmt, ##__VA_ARGS__)

// per-level macros (no logger)
#define ELOG_FATAL(fmt, ...) ELOG(elog::ELEVEL_FATAL, fmt, ##__VA_ARGS__)
#define ELOG_ERROR(fmt, ...) ELOG(elog::ELEVEL_ERROR, fmt, ##__VA_ARGS__)
#define ELOG_WARN(fmt, ...) ELOG(elog::ELEVEL_WARN, fmt, ##__VA_ARGS__)
#define ELOG_NOTICE(fmt, ...) ELOG(elog::ELEVEL_NOTICE, fmt, ##__VA_ARGS__)
#define ELOG_INFO(fmt, ...) ELOG(elog::ELEVEL_INFO, fmt, ##__VA_ARGS__)
#define ELOG_TRACE(fmt, ...) ELOG(elog::ELEVEL_TRACE, fmt, ##__VA_ARGS__)
#define ELOG_DEBUG(fmt, ...) ELOG(elog::ELEVEL_DEBUG, fmt, ##__VA_ARGS__)
#define ELOG_DIAG(fmt, ...) ELOG(elog::ELEVEL_DIAG, fmt, ##__VA_ARGS__)

/**************************************************************************************
 *                          fmtlib Logging Macros
 **************************************************************************************/

#ifdef ELOG_ENABLE_FMT_LIB

#define ELOG_FMT_BASE(logger, level, fmtStr, ...)                                      \
    {                                                                                  \
        std::string logMsg = fmt::format(fmtStr, ##__VA_ARGS__);                       \
        logger->logNoFormat(level, __FILE__, __LINE__, ELOG_FUNCTION, logMsg.c_str()); \
    }

/**
 * @brief Logs a formatted message (fmtlib style).
 * @param logger The logger used for message formatting.
 * @param level The log level. If the log level is insufficient, then the message is dropped.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_FMT_EX(logger, level, fmtStr, ...)                       \
    {                                                                 \
        elog::ELogLogger* validLogger = elog::getValidLogger(logger); \
        if (validLogger->canLog(level)) {                             \
            ELOG_FMT_BASE(validLogger, level, fmtStr, ##__VA_ARGS__); \
        }                                                             \
    }

// per-level macros
#define ELOG_FMT_FATAL_EX(logger, fmt, ...) \
    ELOG_FMT_EX(logger, elog::ELEVEL_FATAL, fmt, ##__VA_ARGS__)
#define ELOG_FMT_ERROR_EX(logger, fmt, ...) \
    ELOG_FMT_EX(logger, elog::ELEVEL_ERROR, fmt, ##__VA_ARGS__)
#define ELOG_FMT_WARN_EX(logger, fmt, ...) \
    ELOG_FMT_EX(logger, elog::ELEVEL_WARN, fmt, ##__VA_ARGS__)
#define ELOG_FMT_NOTICE_EX(logger, fmt, ...) \
    ELOG_FMT_EX(logger, elog::ELEVEL_NOTICE, fmt, ##__VA_ARGS__)
#define ELOG_FMT_INFO_EX(logger, fmt, ...) \
    ELOG_FMT_EX(logger, elog::ELEVEL_INFO, fmt, ##__VA_ARGS__)
#define ELOG_FMT_TRACE_EX(logger, fmt, ...) \
    ELOG_FMT_EX(logger, elog::ELEVEL_TRACE, fmt, ##__VA_ARGS__)
#define ELOG_FMT_DEBUG_EX(logger, fmt, ...) \
    ELOG_FMT_EX(logger, elog::ELEVEL_DEBUG, fmt, ##__VA_ARGS__)
#define ELOG_FMT_DIAG_EX(logger, fmt, ...) \
    ELOG_FMT_EX(logger, elog::ELEVEL_DIAG, fmt, ##__VA_ARGS__)

/** @brief Log formatted message (fmtlib style, no logger). */
#define ELOG_FMT(level, fmt, ...) ELOG_FMT_EX(nullptr, level, fmt, ##__VA_ARGS__)

// per-level macros (no logger)
#define ELOG_FMT_FATAL(fmt, ...) ELOG_FMT(elog::ELEVEL_FATAL, fmt, ##__VA_ARGS__)
#define ELOG_FMT_ERROR(fmt, ...) ELOG_FMT(elog::ELEVEL_ERROR, fmt, ##__VA_ARGS__)
#define ELOG_FMT_WARN(fmt, ...) ELOG_FMT(elog::ELEVEL_WARN, fmt, ##__VA_ARGS__)
#define ELOG_FMT_NOTICE(fmt, ...) ELOG_FMT(elog::ELEVEL_NOTICE, fmt, ##__VA_ARGS__)
#define ELOG_FMT_INFO(fmt, ...) ELOG_FMT(elog::ELEVEL_INFO, fmt, ##__VA_ARGS__)
#define ELOG_FMT_TRACE(fmt, ...) ELOG_FMT(elog::ELEVEL_TRACE, fmt, ##__VA_ARGS__)
#define ELOG_FMT_DEBUG(fmt, ...) ELOG_FMT(elog::ELEVEL_DEBUG, fmt, ##__VA_ARGS__)
#define ELOG_FMT_DIAG(fmt, ...) ELOG_FMT(elog::ELEVEL_DIAG, fmt, ##__VA_ARGS__)
#endif

/**************************************************************************************
 *                          Binary Logging Macros
 **************************************************************************************/

#ifdef ELOG_ENABLE_FMT_LIB
#define ELOG_BIN_BASE(logger, level, fmt, ...) \
    logger->logBinary(level, __FILE__, __LINE__, ELOG_FUNCTION, fmt, ##__VA_ARGS__)

/**
 * @brief Logs a formatted message (fmtlib style, binary form).
 * @param logger The logger used for message formatting.
 * @param level The log level. If the log level is insufficient, then the message is dropped.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_BIN_EX(logger, level, fmt, ...)                          \
    {                                                                 \
        elog::ELogLogger* validLogger = elog::getValidLogger(logger); \
        if (validLogger->canLog(level)) {                             \
            ELOG_BIN_BASE(validLogger, level, fmt, ##__VA_ARGS__);    \
        }                                                             \
    }

// per-level macros
#define ELOG_BIN_FATAL_EX(logger, fmt, ...) \
    ELOG_BIN_EX(logger, elog::ELEVEL_FATAL, fmt, ##__VA_ARGS__)
#define ELOG_BIN_ERROR_EX(logger, fmt, ...) \
    ELOG_BIN_EX(logger, elog::ELEVEL_ERROR, fmt, ##__VA_ARGS__)
#define ELOG_BIN_WARN_EX(logger, fmt, ...) \
    ELOG_BIN_EX(logger, elog::ELEVEL_WARN, fmt, ##__VA_ARGS__)
#define ELOG_BIN_NOTICE_EX(logger, fmt, ...) \
    ELOG_BIN_EX(logger, elog::ELEVEL_NOTICE, fmt, ##__VA_ARGS__)
#define ELOG_BIN_INFO_EX(logger, fmt, ...) \
    ELOG_BIN_EX(logger, elog::ELEVEL_INFO, fmt, ##__VA_ARGS__)
#define ELOG_BIN_TRACE_EX(logger, fmt, ...) \
    ELOG_BIN_EX(logger, elog::ELEVEL_TRACE, fmt, ##__VA_ARGS__)
#define ELOG_BIN_DEBUG_EX(logger, fmt, ...) \
    ELOG_BIN_EX(logger, elog::ELEVEL_DEBUG, fmt, ##__VA_ARGS__)
#define ELOG_BIN_DIAG_EX(logger, fmt, ...) \
    ELOG_BIN_EX(logger, elog::ELEVEL_DIAG, fmt, ##__VA_ARGS__)

/** @brief Log formatted message (fmtlib style, binary form, no logger). */
#define ELOG_BIN(level, fmt, ...) ELOG_BIN_EX(nullptr, level, fmt, ##__VA_ARGS__);

// per-level macros (no logger)
#define ELOG_BIN_FATAL(fmt, ...) ELOG_BIN(elog::ELEVEL_FATAL, fmt, ##__VA_ARGS__)
#define ELOG_BIN_ERROR(fmt, ...) ELOG_BIN(elog::ELEVEL_ERROR, fmt, ##__VA_ARGS__)
#define ELOG_BIN_WARN(fmt, ...) ELOG_BIN(elog::ELEVEL_WARN, fmt, ##__VA_ARGS__)
#define ELOG_BIN_NOTICE(fmt, ...) ELOG_BIN(elog::ELEVEL_NOTICE, fmt, ##__VA_ARGS__)
#define ELOG_BIN_INFO(fmt, ...) ELOG_BIN(elog::ELEVEL_INFO, fmt, ##__VA_ARGS__)
#define ELOG_BIN_TRACE(fmt, ...) ELOG_BIN(elog::ELEVEL_TRACE, fmt, ##__VA_ARGS__)
#define ELOG_BIN_DEBUG(fmt, ...) ELOG_BIN(elog::ELEVEL_DEBUG, fmt, ##__VA_ARGS__)
#define ELOG_BIN_DIAG(fmt, ...) ELOG_BIN(elog::ELEVEL_DIAG, fmt, ##__VA_ARGS__)
#endif

/**************************************************************************************
 *                          Auto-Cached Logging Macros
 **************************************************************************************/

#ifdef ELOG_ENABLE_FMT_LIB
#define ELOG_CACHE_BASE(logger, level, fmt, ...)                                                  \
    {                                                                                             \
        static thread_local elog::ELogCacheEntryId cacheEntryId = elog::getOrCacheFormatMsg(fmt); \
        logger->logBinaryCached(level, __FILE__, __LINE__, ELOG_FUNCTION, cacheEntryId,           \
                                ##__VA_ARGS__);                                                   \
    }

/**
 * @brief Logs a formatted message (fmtlib style, binary form, auto-cached).
 * @param logger The logger used for message formatting.
 * @param level The log level. If the log level is insufficient, then the message is dropped.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_CACHE_EX(logger, level, fmt, ...)                        \
    {                                                                 \
        elog::ELogLogger* validLogger = elog::getValidLogger(logger); \
        if (validLogger->canLog(level)) {                             \
            ELOG_CACHE_BASE(validLogger, level, fmt, ##__VA_ARGS__);  \
        }                                                             \
    }

// per-level macros
#define ELOG_CACHE_FATAL_EX(logger, fmt, ...) \
    ELOG_CACHE_EX(logger, elog::ELEVEL_FATAL, fmt, ##__VA_ARGS__)
#define ELOG_CACHE_ERROR_EX(logger, fmt, ...) \
    ELOG_CACHE_EX(logger, elog::ELEVEL_ERROR, fmt, ##__VA_ARGS__)
#define ELOG_CACHE_WARN_EX(logger, fmt, ...) \
    ELOG_CACHE_EX(logger, elog::ELEVEL_WARN, fmt, ##__VA_ARGS__)
#define ELOG_CACHE_NOTICE_EX(logger, fmt, ...) \
    ELOG_CACHE_EX(logger, elog::ELEVEL_NOTICE, fmt, ##__VA_ARGS__)
#define ELOG_CACHE_INFO_EX(logger, fmt, ...) \
    ELOG_CACHE_EX(logger, elog::ELEVEL_INFO, fmt, ##__VA_ARGS__)
#define ELOG_CACHE_TRACE_EX(logger, fmt, ...) \
    ELOG_CACHE_EX(logger, elog::ELEVEL_TRACE, fmt, ##__VA_ARGS__)
#define ELOG_CACHE_DEBUG_EX(logger, fmt, ...) \
    ELOG_CACHE_EX(logger, elog::ELEVEL_DEBUG, fmt, ##__VA_ARGS__)
#define ELOG_CACHE_DIAG_EX(logger, fmt, ...) \
    ELOG_CACHE_EX(logger, elog::ELEVEL_DIAG, fmt, ##__VA_ARGS__)

/** @brief Log formatted message (fmtlib style, binary form, auto-cached, no logger). */
#define ELOG_CACHE(level, fmt, ...) ELOG_CACHE_EX(nullptr, level, fmt, ##__VA_ARGS__);

// per-level macros (no logger)
#define ELOG_CACHE_FATAL(fmt, ...) ELOG_CACHE(elog::ELEVEL_FATAL, fmt, ##__VA_ARGS__)
#define ELOG_CACHE_ERROR(fmt, ...) ELOG_CACHE(elog::ELEVEL_ERROR, fmt, ##__VA_ARGS__)
#define ELOG_CACHE_WARN(fmt, ...) ELOG_CACHE(elog::ELEVEL_WARN, fmt, ##__VA_ARGS__)
#define ELOG_CACHE_NOTICE(fmt, ...) ELOG_CACHE(elog::ELEVEL_NOTICE, fmt, ##__VA_ARGS__)
#define ELOG_CACHE_INFO(fmt, ...) ELOG_CACHE(elog::ELEVEL_INFO, fmt, ##__VA_ARGS__)
#define ELOG_CACHE_TRACE(fmt, ...) ELOG_CACHE(elog::ELEVEL_TRACE, fmt, ##__VA_ARGS__)
#define ELOG_CACHE_DEBUG(fmt, ...) ELOG_CACHE(elog::ELEVEL_DEBUG, fmt, ##__VA_ARGS__)
#define ELOG_CACHE_DIAG(fmt, ...) ELOG_CACHE(elog::ELEVEL_DIAG, fmt, ##__VA_ARGS__)
#endif

/**************************************************************************************
 *                          Pre-Cached Logging Macros
 **************************************************************************************/

#ifdef ELOG_ENABLE_FMT_LIB
#define ELOG_ID_BASE(logger, level, cacheEntryId, ...) \
    logger->logBinaryCached(level, __FILE__, __LINE__, ELOG_FUNCTION, cacheEntryId, ##__VA_ARGS__);

/**
 * @brief Logs a formatted message (fmtlib style, binary form, pre-cached).
 * @param logger The logger used for message formatting.
 * @param level The log level. If the log level is insufficient, then the message is dropped.
 * @param cacheEntryId The cached log message format string id.
 * @param ... Log message format string parameters.
 */
#define ELOG_ID_EX(logger, level, cacheEntryId, ...)                       \
    {                                                                      \
        elog::ELogLogger* validLogger = elog::getValidLogger(logger);      \
        if (validLogger->canLog(level)) {                                  \
            ELOG_ID_BASE(validLogger, level, cacheEntryId, ##__VA_ARGS__); \
        }                                                                  \
    }

// per-level macros
#define ELOG_ID_FATAL_EX(logger, cacheEntryId, ...) \
    ELOG_ID_EX(logger, elog::ELEVEL_FATAL, cacheEntryId, ##__VA_ARGS__)
#define ELOG_ID_ERROR_EX(logger, cacheEntryId, ...) \
    ELOG_ID_EX(logger, elog::ELEVEL_ERROR, cacheEntryId, ##__VA_ARGS__)
#define ELOG_ID_WARN_EX(logger, cacheEntryId, ...) \
    ELOG_ID_EX(logger, elog::ELEVEL_WARN, cacheEntryId, ##__VA_ARGS__)
#define ELOG_ID_NOTICE_EX(logger, cacheEntryId, ...) \
    ELOG_ID_EX(logger, elog::ELEVEL_NOTICE, cacheEntryId, ##__VA_ARGS__)
#define ELOG_ID_INFO_EX(logger, cacheEntryId, ...) \
    ELOG_ID_EX(logger, elog::ELEVEL_INFO, cacheEntryId, ##__VA_ARGS__)
#define ELOG_ID_TRACE_EX(logger, cacheEntryId, ...) \
    ELOG_ID_EX(logger, elog::ELEVEL_TRACE, cacheEntryId, ##__VA_ARGS__)
#define ELOG_ID_DEBUG_EX(logger, cacheEntryId, ...) \
    ELOG_ID_EX(logger, elog::ELEVEL_DEBUG, cacheEntryId, ##__VA_ARGS__)
#define ELOG_ID_DIAG_EX(logger, cacheEntryId, ...) \
    ELOG_ID_EX(logger, elog::ELEVEL_DIAG, cacheEntryId, ##__VA_ARGS__)

/** @brief Log formatted message (fmtlib style, binary form, pre-cached, no logger). */
#define ELOG_ID(level, cacheEntryId, ...) ELOG_ID_EX(nullptr, level, cacheEntryId, ##__VA_ARGS__);

// per-level macros (no logger)
#define ELOG_ID_FATAL(cacheEntryId, ...) ELOG_ID(elog::ELEVEL_FATAL, cacheEntryId, ##__VA_ARGS__)
#define ELOG_ID_ERROR(cacheEntryId, ...) ELOG_ID(elog::ELEVEL_ERROR, cacheEntryId, ##__VA_ARGS__)
#define ELOG_ID_WARN(cacheEntryId, ...) ELOG_ID(elog::ELEVEL_WARN, cacheEntryId, ##__VA_ARGS__)
#define ELOG_ID_NOTICE(cacheEntryId, ...) ELOG_ID(elog::ELEVEL_NOTICE, cacheEntryId, ##__VA_ARGS__)
#define ELOG_ID_INFO(cacheEntryId, ...) ELOG_ID(elog::ELEVEL_INFO, cacheEntryId, ##__VA_ARGS__)
#define ELOG_ID_TRACE(cacheEntryId, ...) ELOG_ID(elog::ELEVEL_TRACE, cacheEntryId, ##__VA_ARGS__)
#define ELOG_ID_DEBUG(cacheEntryId, ...) ELOG_ID(elog::ELEVEL_DEBUG, cacheEntryId, ##__VA_ARGS__)
#define ELOG_ID_DIAG(cacheEntryId, ...) ELOG_ID(elog::ELEVEL_DIAG, cacheEntryId, ##__VA_ARGS__)
#endif

/**************************************************************************************
 *                 Continued (in-parts) Logging Macros
 **************************************************************************************/

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

#ifdef ELOG_ENABLE_FMT_LIB
/** @brief Begins a multi-part log message (fmtlib style). */
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
 * @param level The log level. If the log level is insufficient, then the message is dropped.
 * @param fmt The message format.
 * @param ... The message arguments.
 */
#define ELOG_APPEND_EX(logger, level, fmt, ...)                       \
    {                                                                 \
        elog::ELogLogger* validLogger = elog::getValidLogger(logger); \
        if (validLogger->canLog(level)) {                             \
            validLogger->appendLog(fmt, ##__VA_ARGS__);               \
        }                                                             \
    }

#ifdef ELOG_ENABLE_FMT_LIB
/** @brief Appends formatted message to a multi-part log message (fmtlib style). */
#define ELOG_FMT_APPEND_EX(logger, level, fmtStr, ...)                \
    {                                                                 \
        elog::ELogLogger* validLogger = elog::getValidLogger(logger); \
        if (validLogger->canLog(level)) {                             \
            std::string logMsg = fmt::format(fmtStr, ##__VA_ARGS__);  \
            validLogger->appendLogNoFormat(logMsg.c_str());           \
        }                                                             \
    }
#endif

/**
 * @brief Appends unformatted message to a multi-part log message.
 * @param logger The logger used for message formatting.
 * @param level The log level. If the log level is insufficient, then the message is dropped.
 * @param msg The log message.
 */
#define ELOG_APPEND_NF_EX(logger, level, msg)                         \
    {                                                                 \
        elog::ELogLogger* validLogger = elog::getValidLogger(logger); \
        if (validLogger->canLog(level)) {                             \
            validLogger->appendLogNoFormat(msg);                      \
        }                                                             \
    }

/**
 * @brief Terminates a multi-part log message and writes it to log.
 * @param logger The logger used for message formatting.
 */
#define ELOG_END_EX(logger) elog::getValidLogger(logger)->finishLog()

// continued logging macros (no logger)
#define ELOG_BEGIN(level, fmt, ...) ELOG_BEGIN_EX(nullptr, level, fmt, ##__VA_ARGS__)
#define ELOG_APPEND(level, fmt, ...) ELOG_APPEND_EX(nullptr, level, fmt, ##__VA_ARGS__)
#define ELOG_APPEND_NF(level, msg) ELOG_APPEND_NF_EX(nullptr, level, msg)
#define ELOG_END() ELOG_END_EX(nullptr)

// continued logging macros (fmtlib style, no logger)
#ifdef ELOG_ENABLE_FMT_LIB
#define ELOG_FMT_BEGIN(level, fmt, ...) ELOG_FMT_BEGIN_EX(nullptr, level, fmt, ##__VA_ARGS__)
#define ELOG_FMT_APPEND(level, fmt, ...) ELOG_FMT_APPEND_EX(nullptr, level, fmt, ##__VA_ARGS__)
#endif

/**************************************************************************************
 *                          System Error Logging Macros
 **************************************************************************************/

/**
 * @brief Logs a system error message with error code.
 * @param logger The logger used for message formatting.
 * @param syscall The system call that failed.
 * @param sysErr The system error code.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_SYS_ERROR_NUM_EX(logger, syscall, sysErr, fmt, ...)                    \
    {                                                                               \
        ELOG_ERROR_EX(logger, "System call " #syscall "() failed: %d (%s)", sysErr, \
                      elog::sysErrorToStr(sysErr));                                 \
        ELOG_ERROR_EX(logger, fmt, ##__VA_ARGS__);                                  \
    }

/** @brief Logs a system error message with error code (no logger). */
#define ELOG_SYS_ERROR_NUM(syscall, sysErr, fmt, ...) \
    ELOG_SYS_ERROR_NUM_EX(nullptr, syscall, sysErr, fmt, ##__VA_ARGS__)

#ifdef ELOG_ENABLE_FMT_LIB
/** @brief Logs a system error message with error code (fmtlib style). */
#define ELOG_FMT_SYS_ERROR_NUM_EX(logger, syscall, sysErr, fmt, ...)                \
    {                                                                               \
        ELOG_ERROR_EX(logger, "System call " #syscall "() failed: %d (%s)", sysErr, \
                      elog::sysErrorToStr(sysErr));                                 \
        ELOG_FMT_ERROR_EX(logger, fmt, ##__VA_ARGS__);                              \
    }

/** @brief Logs a system error message with error code (fmtlib style, no logger). */
#define ELOG_FMT_SYS_ERROR_NUM(syscall, sysErr, fmt, ...) \
    ELOG_FMT_SYS_ERROR_NUM_EX(nullptr, syscall, sysErr, fmt, ##__VA_ARGS__)
#endif

/**
 * @brief Logs a system error message with error code from errno.
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

/** @brief Logs a system error message with error code from errno (no logger). */
#define ELOG_SYS_ERROR(syscall, fmt, ...)                        \
    {                                                            \
        int sysErr = errno;                                      \
        ELOG_SYS_ERROR_NUM(syscall, sysErr, fmt, ##__VA_ARGS__); \
    }

#ifdef ELOG_ENABLE_FMT_LIB
/** @brief Logs a system error message with error code from errno (fmtlib style). */
#define ELOG_FMT_SYS_ERROR_EX(logger, syscall, fmt, ...)                        \
    {                                                                           \
        int sysErr = errno;                                                     \
        ELOG_FMT_SYS_ERROR_NUM_EX(logger, syscall, sysErr, fmt, ##__VA_ARGS__); \
    }

/** @brief Logs a system error message with error code from errno (fmtlib style, no logger). */
#define ELOG_FMT_SYS_ERROR(syscall, fmt, ...)                        \
    {                                                                \
        int sysErr = errno;                                          \
        ELOG_FMT_SYS_ERROR_NUM(syscall, sysErr, fmt, ##__VA_ARGS__); \
    }
#endif

/**************************************************************************************
 *                          Windows System Error Logging Macros
 **************************************************************************************/

#ifdef ELOG_WINDOWS
/**
 * @brief Logs a system error message with error code.
 * @param logger The logger used for message formatting.
 * @param syscall The system call that failed.
 * @param sysErr The system error code.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_WIN32_ERROR_NUM_EX(logger, syscall, sysErr, fmt, ...)                           \
    {                                                                                        \
        char* errStr = elog::win32SysErrorToStr(sysErr);                                     \
        ELOG_ERROR_EX(logger, "Windows system call " #syscall "() failed: %lu (%s)", sysErr, \
                      errStr);                                                               \
        elog::win32FreeErrorStr(errStr);                                                     \
        ELOG_ERROR_EX(logger, fmt, ##__VA_ARGS__);                                           \
    }

/** @brief Logs a system error message with error code (no logger). */
#define ELOG_WIN32_ERROR_NUM(syscall, sysErr, fmt, ...) \
    ELOG_WIN32_ERROR_NUM_EX(nullptr, syscall, sysErr, fmt, ##__VA_ARGS__)

#ifdef ELOG_ENABLE_FMT_LIB
/** @brief Logs a system error message with error code (fmtlib style). */
#define ELOG_FMT_WIN32_ERROR_NUM_EX(logger, syscall, sysErr, fmt, ...)                      \
    {                                                                                       \
        char* errStr = elog::win32SysErrorToStr(sysErr);                                    \
        ELOG_ERROR_EX(logger, "Windows system call " #syscall "() failed: %d (%s)", sysErr, \
                      errStr);                                                              \
        elog::win32FreeErrorStr(errStr);                                                    \
        ELOG_FMT_ERROR_EX(logger, fmt, ##__VA_ARGS__);                                      \
    }

/** @brief Logs a system error message with error code (fmtlib style, no logger). */
#define ELOG_FMT_WIN32_ERROR_NUM(syscall, sysErr, fmt, ...) \
    ELOG_FMT_WIN32_ERROR_NUM_EX(nullptr, syscall, sysErr, fmt, ##__VA_ARGS__)
#endif

/**
 * @brief Logs a system error message with error code from GetLastError().
 * @param logger The logger used for message formatting.
 * @param syscall The system call that failed.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 * @note The error code for the system call is obtained through @ref GetLastError(). If you wish to
 * provide another error code then consider calling @ref ELOG_WIN32_ERROR_NUM_EX() instead.
 */
#define ELOG_WIN32_ERROR_EX(logger, syscall, fmt, ...)                        \
    {                                                                         \
        DWORD sysErr = ::GetLastError();                                      \
        ELOG_WIN32_ERROR_NUM_EX(logger, syscall, sysErr, fmt, ##__VA_ARGS__); \
    }

/** @brief Logs a system error message with error code from GetLastError() (no logger). */
#define ELOG_WIN32_ERROR(syscall, fmt, ...)                        \
    {                                                              \
        DWORD sysErr = ::GetLastError();                           \
        ELOG_WIN32_ERROR_NUM(syscall, sysErr, fmt, ##__VA_ARGS__); \
    }

#ifdef ELOG_ENABLE_FMT_LIB
/** @brief Logs a system error message with error code from GetLastError() (fmtlib style). */
#define ELOG_FMT_WIN32_ERROR_EX(logger, syscall, fmt, ...)                        \
    {                                                                             \
        DWORD sysErr = ::GetLastError();                                          \
        ELOG_FMT_WIN32_ERROR_NUM_EX(logger, syscall, sysErr, fmt, ##__VA_ARGS__); \
    }

/**
 * @brief Logs a system error message with error code from GetLastError() (fmtlib style, no
 * logger).
 */
#define ELOG_FMT_WIN32_ERROR(syscall, fmt, ...)                        \
    {                                                                  \
        DWORD sysErr = ::GetLastError();                               \
        ELOG_FMT_WIN32_ERROR_NUM(syscall, sysErr, fmt, ##__VA_ARGS__); \
    }

#endif

#endif  // ELOG_WINDOWS

/**************************************************************************************
 *                          Stack Trace Logging Macros
 **************************************************************************************/

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
        if (validLogger->canLog(level)) {                             \
            ELOG_BASE(validLogger, level, fmt, ##__VA_ARGS__);        \
            elog::logStackTrace(validLogger, level, title, skip);     \
        }                                                             \
    }

#ifdef ELOG_ENABLE_FMT_LIB
/** @brief Logs the stack trace of the current thread (fmtlib style). */
#define ELOG_FMT_STACK_TRACE_EX(logger, level, title, skip, fmt, ...) \
    {                                                                 \
        elog::ELogLogger* validLogger = elog::getValidLogger(logger); \
        if (validLogger->canLog(level)) {                             \
            ELOG_FMT_BASE(validLogger, level, fmt, ##__VA_ARGS__);    \
            elog::logStackTrace(validLogger, level, title, skip);     \
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
        if (validLogger->canLog(level)) {                             \
            ELOG_BASE(validLogger, level, fmt, ##__VA_ARGS__);        \
            elog::logAppStackTrace(validLogger, level, title, skip);  \
        }                                                             \
    }

#ifdef ELOG_ENABLE_FMT_LIB
/** @brief Logs the stack trace of all running threads in the application (fmtlib style). */
#define ELOG_FMT_APP_STACK_TRACE_EX(logger, level, title, skip, fmt, ...) \
    {                                                                     \
        elog::ELogLogger* validLogger = elog::getValidLogger(logger);     \
        if (validLogger->canLog(level)) {                                 \
            ELOG_FMT_BASE(validLogger, level, fmt, ##__VA_ARGS__);        \
            elog::logAppStackTrace(validLogger0, level, title, skip);     \
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
#define ELOG_STACK_TRACE(level, title, skip, fmt, ...) \
    ELOG_STACK_TRACE_EX(nullptr, level, title, skip, fmt, ##__VA_ARGS__)

#ifdef ELOG_ENABLE_FMT_LIB
/** @brief Logs the stack trace of the current thread, using default logger (fmtlib style). */
#define ELOG_FMT_STACK_TRACE(level, title, skip, fmt, ...) \
    ELOG_FMT_STACK_TRACE_EX(nullptr, level, title, skip, fmt, ##__VA_ARGS__)
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
#define ELOG_APP_STACK_TRACE(level, title, skip, fmt, ...) \
    ELOG_APP_STACK_TRACE_EX(nullptr, level, title, skip, fmt, ##__VA_ARGS__)

#ifdef ELOG_ENABLE_FMT_LIB
/**
 * @brief Logs the stack trace of all running threads in the application, using default logger
 * (fmtlib style).
 */
#define ELOG_FMT_APP_STACK_TRACE(level, title, skip, fmt, ...) \
    ELOG_FMT_APP_STACK_TRACE_EX(nullptr, level, title, skip, fmt, ##__VA_ARGS__)
#endif

#endif  // ELOG_ENABLE_STACK_TRACE

/**************************************************************************************
 *                          Normal Once Logging Macros
 **************************************************************************************/

/**
 * @brief Logs a formatted message, only once in the entire life-time of the application.
 * @param logger The logger used for message formatting.
 * @param level The log level. If the log level is insufficient, then the message is dropped.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_ONCE_EX(logger, level, fmt, ...)                         \
    {                                                                 \
        elog::ELogLogger* validLogger = elog::getValidLogger(logger); \
        if (validLogger->canLog(level)) {                             \
            static elog::ELogOnce once;                               \
            if (once) {                                               \
                ELOG_BASE(validLogger, level, fmt, ##__VA_ARGS__);    \
            }                                                         \
        }                                                             \
    }

// per-level once logging macros
#define ELOG_ONCE_FATAL_EX(logger, fmt, ...) \
    ELOG_ONCE_EX(logger, elog::ELEVEL_FATAL, fmt, ##__VA_ARGS__)
#define ELOG_ONCE_ERROR_EX(logger, fmt, ...) \
    ELOG_ONCE_EX(logger, elog::ELEVEL_ERROR, fmt, ##__VA_ARGS__)
#define ELOG_ONCE_WARN_EX(logger, fmt, ...) \
    ELOG_ONCE_EX(logger, elog::ELEVEL_WARN, fmt, ##__VA_ARGS__)
#define ELOG_ONCE_NOTICE_EX(logger, fmt, ...) \
    ELOG_ONCE_EX(logger, elog::ELEVEL_NOTICE, fmt, ##__VA_ARGS__)
#define ELOG_ONCE_INFO_EX(logger, fmt, ...) \
    ELOG_ONCE_EX(logger, elog::ELEVEL_INFO, fmt, ##__VA_ARGS__)
#define ELOG_ONCE_TRACE_EX(logger, fmt, ...) \
    ELOG_ONCE_EX(logger, elog::ELEVEL_TRACE, fmt, ##__VA_ARGS__)
#define ELOG_ONCE_DEBUG_EX(logger, fmt, ...) \
    ELOG_ONCE_EX(logger, elog::ELEVEL_DEBUG, fmt, ##__VA_ARGS__)
#define ELOG_ONCE_DIAG_EX(logger, fmt, ...) \
    ELOG_ONCE_EX(logger, elog::ELEVEL_DIAG, fmt, ##__VA_ARGS__)

/**
 * @brief Logs a formatted message, only once in the entire life-time of the application (no
 * logger).
 */
#define ELOG_ONCE(level, fmt, ...) ELOG_ONCE_EX(nullptr, level, fmt, ##__VA_ARGS__)

// per-level once logging macros (no logger)
#define ELOG_ONCE_FATAL(fmt, ...) ELOG_ONCE(elog::ELEVEL_FATAL, fmt, ##__VA_ARGS__)
#define ELOG_ONCE_ERROR(fmt, ...) ELOG_ONCE(elog::ELEVEL_ERROR, fmt, ##__VA_ARGS__)
#define ELOG_ONCE_WARN(fmt, ...) ELOG_ONCE(elog::ELEVEL_WARN, fmt, ##__VA_ARGS__)
#define ELOG_ONCE_NOTICE(fmt, ...) ELOG_ONCE(elog::ELEVEL_NOTICE, fmt, ##__VA_ARGS__)
#define ELOG_ONCE_INFO(fmt, ...) ELOG_ONCE(elog::ELEVEL_INFO, fmt, ##__VA_ARGS__)
#define ELOG_ONCE_TRACE(fmt, ...) ELOG_ONCE(elog::ELEVEL_TRACE, fmt, ##__VA_ARGS__)
#define ELOG_ONCE_DEBUG(fmt, ...) ELOG_ONCE(elog::ELEVEL_DEBUG, fmt, ##__VA_ARGS__)
#define ELOG_ONCE_DIAG(fmt, ...) ELOG_ONCE(elog::ELEVEL_DIAG, fmt, ##__VA_ARGS__)

/**************************************************************************************
 *                          fmtlib Once Logging Macros
 **************************************************************************************/

#ifdef ELOG_ENABLE_FMT_LIB
/**
 * @brief Logs a formatted message, only once in the entire life-time of the application (fmtlib
 * style).
 * @param logger The logger used for message formatting.
 * @param level The log level. If the log level is insufficient, then the message is dropped.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_FMT_ONCE_EX(logger, level, fmtStr, ...)                      \
    {                                                                     \
        elog::ELogLogger* validLogger = elog::getValidLogger(logger);     \
        if (validLogger->canLog(level)) {                                 \
            static elog::ELogOnce once;                                   \
            if (once) {                                                   \
                ELOG_FMT_BASE(validLogger, level, fmtStr, ##__VA_ARGS__); \
            }                                                             \
        }                                                                 \
    }

// per-level once logging macros (fmtlib style)
#define ELOG_FMT_ONCE_FATAL_EX(logger, fmt, ...) \
    ELOG_FMT_ONCE_EX(logger, elog::ELEVEL_FATAL, fmt, ##__VA_ARGS__)
#define ELOG_FMT_ONCE_ERROR_EX(logger, fmt, ...) \
    ELOG_FMT_ONCE_EX(logger, elog::ELEVEL_ERROR, fmt, ##__VA_ARGS__)
#define ELOG_FMT_ONCE_WARN_EX(logger, fmt, ...) \
    ELOG_FMT_ONCE_EX(logger, elog::ELEVEL_WARN, fmt, ##__VA_ARGS__)
#define ELOG_FMT_ONCE_NOTICE_EX(logger, fmt, ...) \
    ELOG_FMT_ONCE_EX(logger, elog::ELEVEL_NOTICE, fmt, ##__VA_ARGS__)
#define ELOG_FMT_ONCE_INFO_EX(logger, fmt, ...) \
    ELOG_FMT_ONCE_EX(logger, elog::ELEVEL_INFO, fmt, ##__VA_ARGS__)
#define ELOG_FMT_ONCE_TRACE_EX(logger, fmt, ...) \
    ELOG_FMT_ONCE_EX(logger, elog::ELEVEL_TRACE, fmt, ##__VA_ARGS__)
#define ELOG_FMT_ONCE_DEBUG_EX(logger, fmt, ...) \
    ELOG_FMT_ONCE_EX(logger, elog::ELEVEL_DEBUG, fmt, ##__VA_ARGS__)
#define ELOG_FMT_ONCE_DIAG_EX(logger, fmt, ...) \
    ELOG_FMT_ONCE_EX(logger, elog::ELEVEL_DIAG, fmt, ##__VA_ARGS__)

// per-level once logging macros (fmtlib style, no logger)
#define ELOG_FMT_ONCE(level, fmt, ...) ELOG_FMT_ONCE_EX(nullptr, level, fmt, ##__VA_ARGS__)

#define ELOG_FMT_ONCE_FATAL(fmt, ...) ELOG_FMT_ONCE(elog::ELEVEL_FATAL, fmt, ##__VA_ARGS__)
#define ELOG_FMT_ONCE_ERROR(fmt, ...) ELOG_FMT_ONCE(elog::ELEVEL_ERROR, fmt, ##__VA_ARGS__)
#define ELOG_FMT_ONCE_WARN(fmt, ...) ELOG_FMT_ONCE(elog::ELEVEL_WARN, fmt, ##__VA_ARGS__)
#define ELOG_FMT_ONCE_NOTICE(fmt, ...) ELOG_FMT_ONCE(elog::ELEVEL_NOTICE, fmt, ##__VA_ARGS__)
#define ELOG_FMT_ONCE_INFO(fmt, ...) ELOG_FMT_ONCE(elog::ELEVEL_INFO, fmt, ##__VA_ARGS__)
#define ELOG_FMT_ONCE_TRACE(fmt, ...) ELOG_FMT_ONCE(elog::ELEVEL_TRACE, fmt, ##__VA_ARGS__)
#define ELOG_FMT_ONCE_DEBUG(fmt, ...) ELOG_FMT_ONCE(elog::ELEVEL_DEBUG, fmt, ##__VA_ARGS__)
#define ELOG_FMT_ONCE_DIAG(fmt, ...) ELOG_FMT_ONCE(elog::ELEVEL_DIAG, fmt, ##__VA_ARGS__)

#endif

/**************************************************************************************
 *                          Binary Once Logging Macros
 **************************************************************************************/

#ifdef ELOG_ENABLE_FMT_LIB
/**
 * @brief Logs a formatted message, only once in the entire life-time of the application (fmtlib
 * style, binary form).
 * @param logger The logger used for message formatting.
 * @param level The log level. If the log level is insufficient, then the message is dropped.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_BIN_ONCE_EX(logger, level, fmt, ...)                      \
    {                                                                  \
        elog::ELogLogger* validLogger = elog::getValidLogger(logger);  \
        if (validLogger->canLog(level)) {                              \
            static elog::ELogOnce once;                                \
            if (once) {                                                \
                ELOG_BIN_BASE(validLogger, level, fmt, ##__VA_ARGS__); \
            }                                                          \
        }                                                              \
    }

// per-level once logging macros (fmtlib style, binary logging)
#define ELOG_BIN_ONCE_FATAL_EX(logger, fmt, ...) \
    ELOG_BIN_ONCE_EX(logger, elog::ELEVEL_FATAL, fmt, ##__VA_ARGS__)
#define ELOG_BIN_ONCE_ERROR_EX(logger, fmt, ...) \
    ELOG_BIN_ONCE_EX(logger, elog::ELEVEL_ERROR, fmt, ##__VA_ARGS__)
#define ELOG_BIN_ONCE_WARN_EX(logger, fmt, ...) \
    ELOG_BIN_ONCE_EX(logger, elog::ELEVEL_WARN, fmt, ##__VA_ARGS__)
#define ELOG_BIN_ONCE_NOTICE_EX(logger, fmt, ...) \
    ELOG_BIN_ONCE_EX(logger, elog::ELEVEL_NOTICE, fmt, ##__VA_ARGS__)
#define ELOG_BIN_ONCE_INFO_EX(logger, fmt, ...) \
    ELOG_BIN_ONCE_EX(logger, elog::ELEVEL_INFO, fmt, ##__VA_ARGS__)
#define ELOG_BIN_ONCE_TRACE_EX(logger, fmt, ...) \
    ELOG_BIN_ONCE_EX(logger, elog::ELEVEL_TRACE, fmt, ##__VA_ARGS__)
#define ELOG_BIN_ONCE_DEBUG_EX(logger, fmt, ...) \
    ELOG_BIN_ONCE_EX(logger, elog::ELEVEL_DEBUG, fmt, ##__VA_ARGS__)
#define ELOG_BIN_ONCE_DIAG_EX(logger, fmt, ...) \
    ELOG_BIN_ONCE_EX(logger, elog::ELEVEL_DIAG, fmt, ##__VA_ARGS__)

// per-level once logging macros (fmtlib style, binary logging, no logger)
#define ELOG_BIN_ONCE(level, fmt, ...) ELOG_BIN_ONCE_EX(nullptr, level, fmt, ##__VA_ARGS__)

#define ELOG_BIN_ONCE_FATAL(fmt, ...) ELOG_BIN_ONCE(elog::ELEVEL_FATAL, fmt, ##__VA_ARGS__)
#define ELOG_BIN_ONCE_ERROR(fmt, ...) ELOG_BIN_ONCE(elog::ELEVEL_ERROR, fmt, ##__VA_ARGS__)
#define ELOG_BIN_ONCE_WARN(fmt, ...) ELOG_BIN_ONCE(elog::ELEVEL_WARN, fmt, ##__VA_ARGS__)
#define ELOG_BIN_ONCE_NOTICE(fmt, ...) ELOG_BIN_ONCE(elog::ELEVEL_NOTICE, fmt, ##__VA_ARGS__)
#define ELOG_BIN_ONCE_INFO(fmt, ...) ELOG_BIN_ONCE(elog::ELEVEL_INFO, fmt, ##__VA_ARGS__)
#define ELOG_BIN_ONCE_TRACE(fmt, ...) ELOG_BIN_ONCE(elog::ELEVEL_TRACE, fmt, ##__VA_ARGS__)
#define ELOG_BIN_ONCE_DEBUG(fmt, ...) ELOG_BIN_ONCE(elog::ELEVEL_DEBUG, fmt, ##__VA_ARGS__)
#define ELOG_BIN_ONCE_DIAG(fmt, ...) ELOG_BIN_ONCE(elog::ELEVEL_DIAG, fmt, ##__VA_ARGS__)
#endif

// NOTE: no pre/auto-caching for once logging macros (logged once, no sense in caching)

/**************************************************************************************
 *                          Normal Once-Thread Logging Macros
 **************************************************************************************/

/**
 * @brief Logs a formatted message, only once in the entire life-time of the current thread.
 * @param logger The logger used for message formatting.
 * @param level The log level. If the log level is insufficient, then the message is dropped.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_ONCE_THREAD_EX(logger, level, fmt, ...)                  \
    {                                                                 \
        elog::ELogLogger* validLogger = elog::getValidLogger(logger); \
        if (validLogger->canLog(level)) {                             \
            static thread_local bool once = false;                    \
            if (!once) {                                              \
                once = true;                                          \
                ELOG_BASE(validLogger, level, fmt, ##__VA_ARGS__);    \
            }                                                         \
        }                                                             \
    }

// per-level once-thread logging macros (fmtlib style, binary logging, no logger)
#define ELOG_ONCE_THREAD_FATAL_EX(logger, fmt, ...) \
    ELOG_ONCE_THREAD_EX(logger, elog::ELEVEL_FATAL, fmt, ##__VA_ARGS__)
#define ELOG_ONCE_THREAD_ERROR_EX(logger, fmt, ...) \
    ELOG_ONCE_THREAD_EX(logger, elog::ELEVEL_ERROR, fmt, ##__VA_ARGS__)
#define ELOG_ONCE_THREAD_WARN_EX(logger, fmt, ...) \
    ELOG_ONCE_THREAD_EX(logger, elog::ELEVEL_WARN, fmt, ##__VA_ARGS__)
#define ELOG_ONCE_THREAD_NOTICE_EX(logger, fmt, ...) \
    ELOG_ONCE_THREAD_EX(logger, elog::ELEVEL_NOTICE, fmt, ##__VA_ARGS__)
#define ELOG_ONCE_THREAD_INFO_EX(logger, fmt, ...) \
    ELOG_ONCE_THREAD_EX(logger, elog::ELEVEL_INFO, fmt, ##__VA_ARGS__)
#define ELOG_ONCE_THREAD_TRACE_EX(logger, fmt, ...) \
    ELOG_ONCE_THREAD_EX(logger, elog::ELEVEL_TRACE, fmt, ##__VA_ARGS__)
#define ELOG_ONCE_THREAD_DEBUG_EX(logger, fmt, ...) \
    ELOG_ONCE_THREAD_EX(logger, elog::ELEVEL_DEBUG, fmt, ##__VA_ARGS__)
#define ELOG_ONCE_THREAD_DIAG_EX(logger, fmt, ...) \
    ELOG_ONCE_THREAD_EX(logger, elog::ELEVEL_DIAG, fmt, ##__VA_ARGS__)

// per-level once-thread logging macros (fmtlib style, binary logging, no logger)
#define ELOG_ONCE_THREAD(level, fmt, ...) ELOG_ONCE_THREAD_EX(nullptr, level, fmt, ##__VA_ARGS__)

#define ELOG_ONCE_THREAD_FATAL(fmt, ...) ELOG_ONCE_THREAD(elog::ELEVEL_FATAL, fmt, ##__VA_ARGS__)
#define ELOG_ONCE_THREAD_ERROR(fmt, ...) ELOG_ONCE_THREAD(elog::ELEVEL_ERROR, fmt, ##__VA_ARGS__)
#define ELOG_ONCE_THREAD_WARN(fmt, ...) ELOG_ONCE_THREAD(elog::ELEVEL_WARN, fmt, ##__VA_ARGS__)
#define ELOG_ONCE_THREAD_NOTICE(fmt, ...) ELOG_ONCE_THREAD(elog::ELEVEL_NOTICE, fmt, ##__VA_ARGS__)
#define ELOG_ONCE_THREAD_INFO(fmt, ...) ELOG_ONCE_THREAD(elog::ELEVEL_INFO, fmt, ##__VA_ARGS__)
#define ELOG_ONCE_THREAD_TRACE(fmt, ...) ELOG_ONCE_THREAD(elog::ELEVEL_TRACE, fmt, ##__VA_ARGS__)
#define ELOG_ONCE_THREAD_DEBUG(fmt, ...) ELOG_ONCE_THREAD(elog::ELEVEL_DEBUG, fmt, ##__VA_ARGS__)
#define ELOG_ONCE_THREAD_DIAG(fmt, ...) ELOG_ONCE_THREAD(elog::ELEVEL_DIAG, fmt, ##__VA_ARGS__)

/**************************************************************************************
 *                          fmtlib Once-Thread Logging Macros
 **************************************************************************************/

#ifdef ELOG_ENABLE_FMT_LIB
/**
 * @brief Logs a formatted message, only once in the entire life-time of the current thread (fmtlib
 * style).
 * @param logger The logger used for message formatting.
 * @param level The log level. If the log level is insufficient, then the message is dropped.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_FMT_ONCE_THREAD_EX(logger, level, fmtStr, ...)               \
    {                                                                     \
        elog::ELogLogger* validLogger = elog::getValidLogger(logger);     \
        if (validLogger->canLog(level)) {                                 \
            static thread_local bool once = false;                        \
            if (!once) {                                                  \
                once = true;                                              \
                ELOG_FMT_BASE(validLogger, level, fmtStr, ##__VA_ARGS__); \
            }                                                             \
        }                                                                 \
    }

// per-level once-thread logging macros (fmtlib style)
#define ELOG_FMT_ONCE_THREAD_FATAL_EX(logger, fmt, ...) \
    ELOG_FMT_ONCE_THREAD_EX(logger, elog::ELEVEL_FATAL, fmt, ##__VA_ARGS__)
#define ELOG_FMT_ONCE_THREAD_ERROR_EX(logger, fmt, ...) \
    ELOG_FMT_ONCE_THREAD_EX(logger, elog::ELEVEL_ERROR, fmt, ##__VA_ARGS__)
#define ELOG_FMT_ONCE_THREAD_WARN_EX(logger, fmt, ...) \
    ELOG_FMT_ONCE_THREAD_EX(logger, elog::ELEVEL_WARN, fmt, ##__VA_ARGS__)
#define ELOG_FMT_ONCE_THREAD_NOTICE_EX(logger, fmt, ...) \
    ELOG_FMT_ONCE_THREAD_EX(logger, elog::ELEVEL_NOTICE, fmt, ##__VA_ARGS__)
#define ELOG_FMT_ONCE_THREAD_INFO_EX(logger, fmt, ...) \
    ELOG_FMT_ONCE_THREAD_EX(logger, elog::ELEVEL_INFO, fmt, ##__VA_ARGS__)
#define ELOG_FMT_ONCE_THREAD_TRACE_EX(logger, fmt, ...) \
    ELOG_FMT_ONCE_THREAD_EX(logger, elog::ELEVEL_TRACE, fmt, ##__VA_ARGS__)
#define ELOG_FMT_ONCE_THREAD_DEBUG_EX(logger, fmt, ...) \
    ELOG_FMT_ONCE_THREAD_EX(logger, elog::ELEVEL_DEBUG, fmt, ##__VA_ARGS__)
#define ELOG_FMT_ONCE_THREAD_DIAG_EX(logger, fmt, ...) \
    ELOG_FMT_ONCE_THREAD_EX(logger, elog::ELEVEL_DIAG, fmt, ##__VA_ARGS__)

// per-level once-thread logging macros (fmtlib style, no logger)
#define ELOG_FMT_ONCE_THREAD(level, fmt, ...) \
    ELOG_FMT_ONCE_THREAD_EX(nullptr, level, fmt, ##__VA_ARGS__)

#define ELOG_FMT_ONCE_THREAD_FATAL(fmt, ...) \
    ELOG_FMT_ONCE_THREAD(elog::ELEVEL_FATAL, fmt, ##__VA_ARGS__)
#define ELOG_FMT_ONCE_THREAD_ERROR(fmt, ...) \
    ELOG_FMT_ONCE_THREAD(elog::ELEVEL_ERROR, fmt, ##__VA_ARGS__)
#define ELOG_FMT_ONCE_THREAD_WARN(fmt, ...) \
    ELOG_FMT_ONCE_THREAD(elog::ELEVEL_WARN, fmt, ##__VA_ARGS__)
#define ELOG_FMT_ONCE_THREAD_NOTICE(fmt, ...) \
    ELOG_FMT_ONCE_THREAD(elog::ELEVEL_NOTICE, fmt, ##__VA_ARGS__)
#define ELOG_FMT_ONCE_THREAD_INFO(fmt, ...) \
    ELOG_FMT_ONCE_THREAD(elog::ELEVEL_INFO, fmt, ##__VA_ARGS__)
#define ELOG_FMT_ONCE_THREAD_TRACE(fmt, ...) \
    ELOG_FMT_ONCE_THREAD(elog::ELEVEL_TRACE, fmt, ##__VA_ARGS__)
#define ELOG_FMT_ONCE_THREAD_DEBUG(fmt, ...) \
    ELOG_FMT_ONCE_THREAD(elog::ELEVEL_DEBUG, fmt, ##__VA_ARGS__)
#define ELOG_FMT_ONCE_THREAD_DIAG(fmt, ...) \
    ELOG_FMT_ONCE_THREAD(elog::ELEVEL_DIAG, fmt, ##__VA_ARGS__)
#endif

/**************************************************************************************
 *                          Binary Once-Thread Logging Macros
 **************************************************************************************/

#ifdef ELOG_ENABLE_FMT_LIB
/**
 * @brief Logs a formatted message, only once in the entire life-time of the current thread (fmtlib
 * style, binary form).
 * @param logger The logger used for message formatting.
 * @param level The log level. If the log level is insufficient, then the message is dropped.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_BIN_ONCE_THREAD_EX(logger, level, fmt, ...)               \
    {                                                                  \
        elog::ELogLogger* validLogger = elog::getValidLogger(logger);  \
        if (validLogger->canLog(level)) {                              \
            static thread_local bool once = false;                     \
            if (!once) {                                               \
                once = true;                                           \
                ELOG_BIN_BASE(validLogger, level, fmt, ##__VA_ARGS__); \
            }                                                          \
        }                                                              \
    }

// per-level once-thread logging macros (fmtlib style, binary logging)
#define ELOG_BIN_ONCE_THREAD_FATAL_EX(logger, fmt, ...) \
    ELOG_BIN_ONCE_THREAD_EX(logger, elog::ELEVEL_FATAL, fmt, ##__VA_ARGS__)
#define ELOG_BIN_ONCE_THREAD_ERROR_EX(logger, fmt, ...) \
    ELOG_BIN_ONCE_THREAD_EX(logger, elog::ELEVEL_ERROR, fmt, ##__VA_ARGS__)
#define ELOG_BIN_ONCE_THREAD_WARN_EX(logger, fmt, ...) \
    ELOG_BIN_ONCE_THREAD_EX(logger, elog::ELEVEL_WARN, fmt, ##__VA_ARGS__)
#define ELOG_BIN_ONCE_THREAD_NOTICE_EX(logger, fmt, ...) \
    ELOG_BIN_ONCE_THREAD_EX(logger, elog::ELEVEL_NOTICE, fmt, ##__VA_ARGS__)
#define ELOG_BIN_ONCE_THREAD_INFO_EX(logger, fmt, ...) \
    ELOG_BIN_ONCE_THREAD_EX(logger, elog::ELEVEL_INFO, fmt, ##__VA_ARGS__)
#define ELOG_BIN_ONCE_THREAD_TRACE_EX(logger, fmt, ...) \
    ELOG_BIN_ONCE_THREAD_EX(logger, elog::ELEVEL_TRACE, fmt, ##__VA_ARGS__)
#define ELOG_BIN_ONCE_THREAD_DEBUG_EX(logger, fmt, ...) \
    ELOG_BIN_ONCE_THREAD_EX(logger, elog::ELEVEL_DEBUG, fmt, ##__VA_ARGS__)
#define ELOG_BIN_ONCE_THREAD_DIAG_EX(logger, fmt, ...) \
    ELOG_BIN_ONCE_THREAD_EX(logger, elog::ELEVEL_DIAG, fmt, ##__VA_ARGS__)

// per-level once-thread logging macros (fmtlib style, binary logging, no logger)
#define ELOG_BIN_ONCE_THREAD(level, fmt, ...) \
    ELOG_BIN_ONCE_THREAD_EX(nullptr, level, fmt, ##__VA_ARGS__)

#define ELOG_BIN_ONCE_THREAD_FATAL(fmt, ...) \
    ELOG_BIN_ONCE_THREAD(elog::ELEVEL_FATAL, fmt, ##__VA_ARGS__)
#define ELOG_BIN_ONCE_THREAD_ERROR(fmt, ...) \
    ELOG_BIN_ONCE_THREAD(elog::ELEVEL_ERROR, fmt, ##__VA_ARGS__)
#define ELOG_BIN_ONCE_THREAD_WARN(fmt, ...) \
    ELOG_BIN_ONCE_THREAD(elog::ELEVEL_WARN, fmt, ##__VA_ARGS__)
#define ELOG_BIN_ONCE_THREAD_NOTICE(fmt, ...) \
    ELOG_BIN_ONCE_THREAD(elog::ELEVEL_NOTICE, fmt, ##__VA_ARGS__)
#define ELOG_BIN_ONCE_THREAD_INFO(fmt, ...) \
    ELOG_BIN_ONCE_THREAD(elog::ELEVEL_INFO, fmt, ##__VA_ARGS__)
#define ELOG_BIN_ONCE_THREAD_TRACE(fmt, ...) \
    ELOG_BIN_ONCE_THREAD(elog::ELEVEL_TRACE, fmt, ##__VA_ARGS__)
#define ELOG_BIN_ONCE_THREAD_DEBUG(fmt, ...) \
    ELOG_BIN_ONCE_THREAD(elog::ELEVEL_DEBUG, fmt, ##__VA_ARGS__)
#define ELOG_BIN_ONCE_THREAD_DIAG(fmt, ...) \
    ELOG_BIN_ONCE_THREAD(elog::ELEVEL_DIAG, fmt, ##__VA_ARGS__)
#endif

// NOTE: no pre/auto-cached once-thread logging macros (logged once, no sense in caching)

/**************************************************************************************
 *                          Normal Moderate Logging Macros
 **************************************************************************************/

/**
 * @brief Logs a formatted message, while moderating its occurrence.
 * @param logger The logger used for message formatting.
 * @param level The log level. If the log level is insufficient, then the message is dropped.
 * @param maxMsg The maximum number of messages that can be printed in a time interval.
 * @param timeout The rate limit timeout interval.
 * @param units The rate limit timeout units.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_MODERATE_EX(logger, level, maxMsg, timeout, units, fmt, ...) \
    {                                                                     \
        elog::ELogLogger* validLogger = elog::getValidLogger(logger);     \
        if (validLogger->canLog(level)) {                                 \
            static elog::ELogModerate mod(fmt, maxMsg, timeout, units);   \
            if (mod.moderate()) {                                         \
                ELOG_BASE(validLogger, level, fmt, ##__VA_ARGS__);        \
            }                                                             \
        }                                                                 \
    }

// per-level moderate logging macros
#define ELOG_MODERATE_FATAL_EX(logger, maxMsg, timeout, units, fmt, ...) \
    ELOG_MODERATE_EX(logger, elog::ELEVEL_FATAL, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#define ELOG_MODERATE_ERROR_EX(logger, maxMsg, timeout, units, fmt, ...) \
    ELOG_MODERATE_EX(logger, elog::ELEVEL_ERROR, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#define ELOG_MODERATE_WARN_EX(logger, maxMsg, timeout, units, fmt, ...) \
    ELOG_MODERATE_EX(logger, elog::ELEVEL_WARN, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#define ELOG_MODERATE_NOTICE_EX(logger, maxMsg, timeout, units, fmt, ...) \
    ELOG_MODERATE_EX(logger, elog::ELEVEL_NOTICE, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#define ELOG_MODERATE_INFO_EX(logger, maxMsg, timeout, units, fmt, ...) \
    ELOG_MODERATE_EX(logger, elog::ELEVEL_INFO, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#define ELOG_MODERATE_TRACE_EX(logger, maxMsg, timeout, units, fmt, ...) \
    ELOG_MODERATE_EX(logger, elog::ELEVEL_TRACE, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#define ELOG_MODERATE_DEBUG_EX(logger, maxMsg, timeout, units, fmt, ...) \
    ELOG_MODERATE_EX(logger, elog::ELEVEL_DEBUG, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#define ELOG_MODERATE_DIAG_EX(logger, maxMsg, timeout, units, fmt, ...) \
    ELOG_MODERATE_EX(logger, elog::ELEVEL_DIAG, maxMsg, timeout, units, fmt, ##__VA_ARGS__)

// per-level moderate logging macros (no logger)
#define ELOG_MODERATE(level, maxMsg, timeout, units, fmt, ...) \
    ELOG_MODERATE_EX(nullptr, level, maxMsg, timeout, units, fmt, ##__VA_ARGS__)

#define ELOG_MODERATE_FATAL(maxMsg, timeout, units, fmt, ...) \
    ELOG_MODERATE(elog::ELEVEL_FATAL, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#define ELOG_MODERATE_ERROR(maxMsg, timeout, units, fmt, ...) \
    ELOG_MODERATE(elog::ELEVEL_ERROR, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#define ELOG_MODERATE_WARN(maxMsg, timeout, units, fmt, ...) \
    ELOG_MODERATE(elog::ELEVEL_WARN, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#define ELOG_MODERATE_NOTICE(maxMsg, timeout, units, fmt, ...) \
    ELOG_MODERATE(elog::ELEVEL_NOTICE, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#define ELOG_MODERATE_INFO(maxMsg, timeout, units, fmt, ...) \
    ELOG_MODERATE(elog::ELEVEL_INFO, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#define ELOG_MODERATE_TRACE(maxMsg, timeout, units, fmt, ...) \
    ELOG_MODERATE(elog::ELEVEL_TRACE, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#define ELOG_MODERATE_DEBUG(maxMsg, timeout, units, fmt, ...) \
    ELOG_MODERATE(elog::ELEVEL_DEBUG, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#define ELOG_MODERATE_DIAG(maxMsg, timeout, units, fmt, ...) \
    ELOG_MODERATE(elog::ELEVEL_DIAG, maxMsg, timeout, units, fmt, ##__VA_ARGS__)

/**************************************************************************************
 *                          fmtlib Moderate Logging Macros
 **************************************************************************************/

#ifdef ELOG_ENABLE_FMT_LIB
/**
 * @brief Logs a formatted message, while moderating its occurrence (fmtlib style).
 * @param logger The logger used for message formatting.
 * @param level The log level. If the log level is insufficient, then the message is dropped.
 * @param maxMsg The maximum number of messages that can be printed in a time interval.
 * @param intervalMillis The time interval in milliseconds.
 * @param fmtStr The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_FMT_MODERATE_EX(logger, level, maxMsg, timeout, units, fmtStr, ...) \
    {                                                                            \
        elog::ELogLogger* validLogger = elog::getValidLogger(logger);            \
        if (validLogger->canLog(level)) {                                        \
            static elog::ELogModerate mod(fmtStr, maxMsg, timeout, units);       \
            if (mod.moderate()) {                                                \
                ELOG_FMT_BASE(validLogger, level, fmtStr, ##__VA_ARGS__);        \
            }                                                                    \
        }                                                                        \
    }

// per-level moderate logging macros (fmtlib style)
#define ELOG_FMT_MODERATE_FATAL_EX(logger, maxMsg, timeout, units, fmt, ...) \
    ELOG_FMT_MODERATE_EX(logger, elog::ELEVEL_FATAL, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#define ELOG_FMT_MODERATE_ERROR_EX(logger, maxMsg, timeout, units, fmt, ...) \
    ELOG_FMT_MODERATE_EX(logger, elog::ELEVEL_ERROR, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#define ELOG_FMT_MODERATE_WARN_EX(logger, maxMsg, timeout, units, fmt, ...) \
    ELOG_FMT_MODERATE_EX(logger, elog::ELEVEL_WARN, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#define ELOG_FMT_MODERATE_NOTICE_EX(logger, maxMsg, timeout, units, fmt, ...) \
    ELOG_FMT_MODERATE_EX(logger, elog::ELEVEL_NOTICE, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#define ELOG_FMT_MODERATE_INFO_EX(logger, maxMsg, timeout, units, fmt, ...) \
    ELOG_FMT_MODERATE_EX(logger, elog::ELEVEL_INFO, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#define ELOG_FMT_MODERATE_TRACE_EX(logger, maxMsg, timeout, units, fmt, ...) \
    ELOG_FMT_MODERATE_EX(logger, elog::ELEVEL_TRACE, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#define ELOG_FMT_MODERATE_DEBUG_EX(logger, maxMsg, timeout, units, fmt, ...) \
    ELOG_FMT_MODERATE_EX(logger, elog::ELEVEL_DEBUG, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#define ELOG_FMT_MODERATE_DIAG_EX(logger, maxMsg, timeout, units, fmt, ...) \
    ELOG_FMT_MODERATE_EX(logger, elog::ELEVEL_DIAG, maxMsg, timeout, units, fmt, ##__VA_ARGS__)

// per-level moderate logging macros (fmtlib style, no logger)
#define ELOG_FMT_MODERATE(level, maxMsg, timeout, units, fmt, ...) \
    ELOG_FMT_MODERATE_EX(nullptr, level, maxMsg, timeout, units, fmt, ##__VA_ARGS__)

#define ELOG_FMT_MODERATE_FATAL(maxMsg, timeout, units, fmt, ...) \
    ELOG_FMT_MODERATE(elog::ELEVEL_FATAL, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#define ELOG_FMT_MODERATE_ERROR(maxMsg, timeout, units, fmt, ...) \
    ELOG_FMT_MODERATE(elog::ELEVEL_ERROR, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#define ELOG_FMT_MODERATE_WARN(maxMsg, timeout, units, fmt, ...) \
    ELOG_FMT_MODERATE(elog::ELEVEL_WARN, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#define ELOG_FMT_MODERATE_NOTICE(maxMsg, timeout, units, fmt, ...) \
    ELOG_FMT_MODERATE(elog::ELEVEL_NOTICE, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#define ELOG_FMT_MODERATE_INFO(maxMsg, timeout, units, fmt, ...) \
    ELOG_FMT_MODERATE(elog::ELEVEL_INFO, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#define ELOG_FMT_MODERATE_TRACE(maxMsg, timeout, units, fmt, ...) \
    ELOG_FMT_MODERATE(elog::ELEVEL_TRACE, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#define ELOG_FMT_MODERATE_DEBUG(maxMsg, timeout, units, fmt, ...) \
    ELOG_FMT_MODERATE(elog::ELEVEL_DEBUG, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#define ELOG_FMT_MODERATE_DIAG(maxMsg, timeout, units, fmt, ...) \
    ELOG_FMT_MODERATE(elog::ELEVEL_DIAG, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#endif

/**************************************************************************************
 *                          Binary Moderate Logging Macros
 **************************************************************************************/

#ifdef ELOG_ENABLE_FMT_LIB
/**
 * @brief Logs a formatted message, while moderating its occurrence (fmtlib style, binary form).
 * @param logger The logger used for message formatting.
 * @param level The log level. If the log level is insufficient, then the message is dropped.
 * @param maxMsg The maximum number of messages that can be printed in a time interval.
 * @param intervalMillis The time interval in milliseconds.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_BIN_MODERATE_EX(logger, level, maxMsg, timeout, units, fmt, ...) \
    {                                                                         \
        elog::ELogLogger* validLogger = elog::getValidLogger(logger);         \
        if (validLogger->canLog(level)) {                                     \
            static elog::ELogModerate mod(fmt, maxMsg, timeout, units);       \
            if (mod.moderate()) {                                             \
                ELOG_BIN_BASE(validLogger, level, fmt, ##__VA_ARGS__);        \
            }                                                                 \
        }                                                                     \
    }

// per-level moderate logging macros (fmtlib style, binary logging)
#define ELOG_BIN_MODERATE_FATAL_EX(logger, maxMsg, timeout, units, fmt, ...) \
    ELOG_BIN_MODERATE_EX(logger, elog::ELEVEL_FATAL, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#define ELOG_BIN_MODERATE_ERROR_EX(logger, maxMsg, timeout, units, fmt, ...) \
    ELOG_BIN_MODERATE_EX(logger, elog::ELEVEL_ERROR, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#define ELOG_BIN_MODERATE_WARN_EX(logger, maxMsg, timeout, units, fmt, ...) \
    ELOG_BIN_MODERATE_EX(logger, elog::ELEVEL_WARN, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#define ELOG_BIN_MODERATE_NOTICE_EX(logger, maxMsg, timeout, units, fmt, ...) \
    ELOG_BIN_MODERATE_EX(logger, elog::ELEVEL_NOTICE, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#define ELOG_BIN_MODERATE_INFO_EX(logger, maxMsg, timeout, units, fmt, ...) \
    ELOG_BIN_MODERATE_EX(logger, elog::ELEVEL_INFO, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#define ELOG_BIN_MODERATE_TRACE_EX(logger, maxMsg, timeout, units, fmt, ...) \
    ELOG_BIN_MODERATE_EX(logger, elog::ELEVEL_TRACE, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#define ELOG_BIN_MODERATE_DEBUG_EX(logger, maxMsg, timeout, units, fmt, ...) \
    ELOG_BIN_MODERATE_EX(logger, elog::ELEVEL_DEBUG, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#define ELOG_BIN_MODERATE_DIAG_EX(logger, maxMsg, timeout, units, fmt, ...) \
    ELOG_BIN_MODERATE_EX(logger, elog::ELEVEL_DIAG, maxMsg, timeout, units, fmt, ##__VA_ARGS__)

// per-level moderate logging macros (fmtlib style, binary logging, no logger)
#define ELOG_BIN_MODERATE(level, maxMsg, timeout, units, fmt, ...) \
    ELOG_BIN_MODERATE_EX(nullptr, level, maxMsg, timeout, units, fmt, ##__VA_ARGS__)

#define ELOG_BIN_MODERATE_FATAL(maxMsg, timeout, units, fmt, ...) \
    ELOG_BIN_MODERATE(elog::ELEVEL_FATAL, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#define ELOG_BIN_MODERATE_ERROR(maxMsg, timeout, units, fmt, ...) \
    ELOG_BIN_MODERATE(elog::ELEVEL_ERROR, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#define ELOG_BIN_MODERATE_WARN(maxMsg, timeout, units, fmt, ...) \
    ELOG_BIN_MODERATE(elog::ELEVEL_WARN, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#define ELOG_BIN_MODERATE_NOTICE(maxMsg, timeout, units, fmt, ...) \
    ELOG_BIN_MODERATE(elog::ELEVEL_NOTICE, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#define ELOG_BIN_MODERATE_INFO(maxMsg, timeout, units, fmt, ...) \
    ELOG_BIN_MODERATE(elog::ELEVEL_INFO, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#define ELOG_BIN_MODERATE_TRACE(maxMsg, timeout, units, fmt, ...) \
    ELOG_BIN_MODERATE(elog::ELEVEL_TRACE, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#define ELOG_BIN_MODERATE_DEBUG(maxMsg, timeout, units, fmt, ...) \
    ELOG_BIN_MODERATE(elog::ELEVEL_DEBUG, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#define ELOG_BIN_MODERATE_DIAG(maxMsg, timeout, units, fmt, ...) \
    ELOG_BIN_MODERATE(elog::ELEVEL_DIAG, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#endif

/**************************************************************************************
 *                          Auto-Cached Moderate Logging Macros
 **************************************************************************************/

#ifdef ELOG_ENABLE_FMT_LIB
/**
 * @brief Logs a formatted message, while moderating its occurrence (fmtlib style, binary form,
 * auto-cached).
 * @param logger The logger used for message formatting.
 * @param level The log level. If the log level is insufficient, then the message is dropped.
 * @param maxMsg The maximum number of messages that can be printed in a time interval.
 * @param intervalMillis The time interval in milliseconds.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_CACHE_MODERATE_EX(logger, level, maxMsg, timeout, units, fmt, ...) \
    {                                                                           \
        elog::ELogLogger* validLogger = elog::getValidLogger(logger);           \
        if (validLogger->canLog(level)) {                                       \
            static thread_local elog::ELogCacheEntryId cacheEntryId =           \
                elog::getOrCacheFormatMsg(fmt);                                 \
            static elog::ELogModerate mod(fmt, maxMsg, timeout, units);         \
            if (mod.moderate()) {                                               \
                ELOG_ID_BASE(validLogger, level, cacheEntryId, ##__VA_ARGS__);  \
            }                                                                   \
        }                                                                       \
    }

// per-level moderate logging macros (fmtlib style, binary logging, auto-cached)
#define ELOG_CACHE_MODERATE_FATAL_EX(logger, maxMsg, timeout, units, fmt, ...) \
    ELOG_CACHE_MODERATE_EX(logger, elog::ELEVEL_FATAL, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#define ELOG_CACHE_MODERATE_ERROR_EX(logger, maxMsg, timeout, units, fmt, ...) \
    ELOG_CACHE_MODERATE_EX(logger, elog::ELEVEL_ERROR, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#define ELOG_CACHE_MODERATE_WARN_EX(logger, maxMsg, timeout, units, fmt, ...) \
    ELOG_CACHE_MODERATE_EX(logger, elog::ELEVEL_WARN, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#define ELOG_CACHE_MODERATE_NOTICE_EX(logger, maxMsg, timeout, units, fmt, ...) \
    ELOG_CACHE_MODERATE_EX(logger, elog::ELEVEL_NOTICE, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#define ELOG_CACHE_MODERATE_INFO_EX(logger, maxMsg, timeout, units, fmt, ...) \
    ELOG_CACHE_MODERATE_EX(logger, elog::ELEVEL_INFO, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#define ELOG_CACHE_MODERATE_TRACE_EX(logger, maxMsg, timeout, units, fmt, ...) \
    ELOG_CACHE_MODERATE_EX(logger, elog::ELEVEL_TRACE, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#define ELOG_CACHE_MODERATE_DEBUG_EX(logger, maxMsg, timeout, units, fmt, ...) \
    ELOG_CACHE_MODERATE_EX(logger, elog::ELEVEL_DEBUG, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#define ELOG_CACHE_MODERATE_DIAG_EX(logger, maxMsg, timeout, units, fmt, ...) \
    ELOG_CACHE_MODERATE_EX(logger, elog::ELEVEL_DIAG, maxMsg, timeout, units, fmt, ##__VA_ARGS__)

// per-level moderate logging macros (fmtlib style, binary logging, auto-cached, no logger)
#define ELOG_CACHE_MODERATE(level, maxMsg, timeout, units, fmt, ...) \
    ELOG_CACHE_MODERATE_EX(nullptr, level, maxMsg, timeout, units, fmt, ##__VA_ARGS__)

#define ELOG_CACHE_MODERATE_FATAL(maxMsg, timeout, units, fmt, ...) \
    ELOG_CACHE_MODERATE(elog::ELEVEL_FATAL, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#define ELOG_CACHE_MODERATE_ERROR(maxMsg, timeout, units, fmt, ...) \
    ELOG_CACHE_MODERATE(elog::ELEVEL_ERROR, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#define ELOG_CACHE_MODERATE_WARN(maxMsg, timeout, units, fmt, ...) \
    ELOG_CACHE_MODERATE(elog::ELEVEL_WARN, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#define ELOG_CACHE_MODERATE_NOTICE(maxMsg, timeout, units, fmt, ...) \
    ELOG_CACHE_MODERATE(elog::ELEVEL_NOTICE, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#define ELOG_CACHE_MODERATE_INFO(maxMsg, timeout, units, fmt, ...) \
    ELOG_CACHE_MODERATE(elog::ELEVEL_INFO, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#define ELOG_CACHE_MODERATE_TRACE(maxMsg, timeout, units, fmt, ...) \
    ELOG_CACHE_MODERATE(elog::ELEVEL_TRACE, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#define ELOG_CACHE_MODERATE_DEBUG(maxMsg, timeout, units, fmt, ...) \
    ELOG_CACHE_MODERATE(elog::ELEVEL_DEBUG, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#define ELOG_CACHE_MODERATE_DIAG(maxMsg, timeout, units, fmt, ...) \
    ELOG_CACHE_MODERATE(elog::ELEVEL_DIAG, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#endif

/**************************************************************************************
 *                          Pre-Cached Moderate Logging Macros
 **************************************************************************************/

#ifdef ELOG_ENABLE_FMT_LIB
/**
 * @brief Logs a formatted message, while moderating its occurrence (fmtlib style, binary form,
 * pre-cached).
 * @param logger The logger used for message formatting.
 * @param level The log level. If the log level is insufficient, then the message is dropped.
 * @param maxMsg The maximum number of messages that can be printed in a time interval.
 * @param intervalMillis The time interval in milliseconds.
 * @param cacheEntryId The cached log message format string id.
 * @param ... Log message format string parameters.
 */
#define ELOG_ID_MODERATE_EX(logger, level, maxMsg, timeout, units, cacheEntryId, ...)              \
    {                                                                                              \
        elog::ELogLogger* validLogger = elog::getValidLogger(logger);                              \
        if (validLogger->canLog(level)) {                                                          \
            static elog::ELogModerate mod(elog::getCachedFormatMsg(cacheEntryId), maxMsg, timeout, \
                                          units);                                                  \
            if (mod.moderate()) {                                                                  \
                ELOG_ID_BASE(validLogger, level, cacheEntryId, ##__VA_ARGS__);                     \
            }                                                                                      \
        }                                                                                          \
    }

// per-level moderate logging macros (fmtlib style, binary logging, pre-cached)
#define ELOG_ID_MODERATE_FATAL_EX(logger, maxMsg, timeout, units, cacheEntryId, ...)      \
    ELOG_ID_MODERATE_EX(logger, elog::ELEVEL_FATAL, maxMsg, timeout, units, cacheEntryId, \
                        ##__VA_ARGS__)
#define ELOG_ID_MODERATE_ERROR_EX(logger, maxMsg, timeout, units, cacheEntryId, ...)      \
    ELOG_ID_MODERATE_EX(logger, elog::ELEVEL_ERROR, maxMsg, timeout, units, cacheEntryId, \
                        ##__VA_ARGS__)
#define ELOG_ID_MODERATE_WARN_EX(logger, maxMsg, timeout, units, cacheEntryId, ...)      \
    ELOG_ID_MODERATE_EX(logger, elog::ELEVEL_WARN, maxMsg, timeout, units, cacheEntryId, \
                        ##__VA_ARGS__)
#define ELOG_ID_MODERATE_NOTICE_EX(logger, maxMsg, timeout, units, cacheEntryId, ...)      \
    ELOG_ID_MODERATE_EX(logger, elog::ELEVEL_NOTICE, maxMsg, timeout, units, cacheEntryId, \
                        ##__VA_ARGS__)
#define ELOG_ID_MODERATE_INFO_EX(logger, maxMsg, timeout, units, cacheEntryId, ...)      \
    ELOG_ID_MODERATE_EX(logger, elog::ELEVEL_INFO, maxMsg, timeout, units, cacheEntryId, \
                        ##__VA_ARGS__)
#define ELOG_ID_MODERATE_TRACE_EX(logger, maxMsg, timeout, units, cacheEntryId, ...)      \
    ELOG_ID_MODERATE_EX(logger, elog::ELEVEL_TRACE, maxMsg, timeout, units, cacheEntryId, \
                        ##__VA_ARGS__)
#define ELOG_ID_MODERATE_DEBUG_EX(logger, maxMsg, timeout, units, cacheEntryId, ...)      \
    ELOG_ID_MODERATE_EX(logger, elog::ELEVEL_DEBUG, maxMsg, timeout, units, cacheEntryId, \
                        ##__VA_ARGS__)
#define ELOG_ID_MODERATE_DIAG_EX(logger, maxMsg, timeout, units, cacheEntryId, ...)      \
    ELOG_ID_MODERATE_EX(logger, elog::ELEVEL_DIAG, maxMsg, timeout, units, cacheEntryId, \
                        ##__VA_ARGS__)

// per-level moderate logging macros (fmtlib style, binary logging, pre-cached, no logger)
#define ELOG_ID_MODERATE(level, maxMsg, timeout, units, cacheEntryId, ...) \
    ELOG_ID_MODERATE_EX(nullptr, level, maxMsg, timeout, units, cacheEntryId, ##__VA_ARGS__)

#define ELOG_ID_MODERATE_FATAL(maxMsg, timeout, units, cacheEntryId, ...) \
    ELOG_ID_MODERATE(elog::ELEVEL_FATAL, maxMsg, timeout, units, cacheEntryId, ##__VA_ARGS__)
#define ELOG_ID_MODERATE_ERROR(maxMsg, timeout, units, cacheEntryId, ...) \
    ELOG_ID_MODERATE(elog::ELEVEL_ERROR, maxMsg, timeout, units, cacheEntryId, ##__VA_ARGS__)
#define ELOG_ID_MODERATE_WARN(maxMsg, timeout, units, cacheEntryId, ...) \
    ELOG_ID_MODERATE(elog::ELEVEL_WARN, maxMsg, timeout, units, cacheEntryId, ##__VA_ARGS__)
#define ELOG_ID_MODERATE_NOTICE(maxMsg, timeout, units, cacheEntryId, ...) \
    ELOG_ID_MODERATE(elog::ELEVEL_NOTICE, maxMsg, timeout, units, cacheEntryId, ##__VA_ARGS__)
#define ELOG_ID_MODERATE_INFO(maxMsg, timeout, units, cacheEntryId, ...) \
    ELOG_ID_MODERATE(elog::ELEVEL_INFO, maxMsg, timeout, units, cacheEntryId, ##__VA_ARGS__)
#define ELOG_ID_MODERATE_TRACE(maxMsg, timeout, units, cacheEntryId, ...) \
    ELOG_ID_MODERATE(elog::ELEVEL_TRACE, maxMsg, timeout, units, cacheEntryId, ##__VA_ARGS__)
#define ELOG_ID_MODERATE_DEBUG(maxMsg, timeout, units, cacheEntryId, ...) \
    ELOG_ID_MODERATE(elog::ELEVEL_DEBUG, maxMsg, timeout, units, cacheEntryId, ##__VA_ARGS__)
#define ELOG_ID_MODERATE_DIAG(maxMsg, timeout, units, cacheEntryId, ...) \
    ELOG_ID_MODERATE(elog::ELEVEL_DIAG, maxMsg, timeout, units, cacheEntryId, ##__VA_ARGS__)
#endif

/**************************************************************************************
 *                          Normal Every-N Logging Macros
 **************************************************************************************/

/**
 * @brief Logs a formatted message, once in every N calls.
 * @param logger The logger used for message formatting.
 * @param level The log level. If the log level is insufficient, then the message is dropped.
 * @param N Specifies to log once in N log messages.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_EVERY_N_EX(logger, level, N, fmt, ...)                       \
    {                                                                     \
        elog::ELogLogger* validLogger = elog::getValidLogger(logger);     \
        if (validLogger->canLog(level)) {                                 \
            static std::atomic<uint64_t> count = 0;                       \
            if (count.fetch_add(1, std::memory_order_relaxed) % N == 0) { \
                ELOG_BASE(validLogger, level, fmt, ##__VA_ARGS__);        \
            }                                                             \
        }                                                                 \
    }

// per-level every-N logging macros
#define ELOG_EVERY_N_FATAL_EX(logger, N, fmt, ...) \
    ELOG_EVERY_N_EX(logger, elog::ELEVEL_FATAL, N, fmt, ##__VA_ARGS__)
#define ELOG_EVERY_N_ERROR_EX(logger, N, fmt, ...) \
    ELOG_EVERY_N_EX(logger, elog::ELEVEL_ERROR, N, fmt, ##__VA_ARGS__)
#define ELOG_EVERY_N_WARN_EX(logger, N, fmt, ...) \
    ELOG_EVERY_N_EX(logger, elog::ELEVEL_WARN, N, fmt, ##__VA_ARGS__)
#define ELOG_EVERY_N_NOTICE_EX(logger, N, fmt, ...) \
    ELOG_EVERY_N_EX(logger, elog::ELEVEL_NOTICE, N, fmt, ##__VA_ARGS__)
#define ELOG_EVERY_N_INFO_EX(logger, N, fmt, ...) \
    ELOG_EVERY_N_EX(logger, elog::ELEVEL_INFO, N, fmt, ##__VA_ARGS__)
#define ELOG_EVERY_N_TRACE_EX(logger, N, fmt, ...) \
    ELOG_EVERY_N_EX(logger, elog::ELEVEL_TRACE, N, fmt, ##__VA_ARGS__)
#define ELOG_EVERY_N_DEBUG_EX(logger, N, fmt, ...) \
    ELOG_EVERY_N_EX(logger, elog::ELEVEL_DEBUG, N, fmt, ##__VA_ARGS__)
#define ELOG_EVERY_N_DIAG_EX(logger, N, fmt, ...) \
    ELOG_EVERY_N_EX(logger, elog::ELEVEL_DIAG, N, fmt, ##__VA_ARGS__)

// per-level every-N logging macros (no logger)
#define ELOG_EVERY_N(level, N, fmt, ...) ELOG_EVERY_N_EX(nullptr, level, N, fmt, ##__VA_ARGS__)

#define ELOG_EVERY_N_FATAL(N, fmt, ...) ELOG_EVERY_N(elog::ELEVEL_FATAL, N, fmt, ##__VA_ARGS__)
#define ELOG_EVERY_N_ERROR(N, fmt, ...) ELOG_EVERY_N(elog::ELEVEL_ERROR, N, fmt, ##__VA_ARGS__)
#define ELOG_EVERY_N_WARN(N, fmt, ...) ELOG_EVERY_N(elog::ELEVEL_WARN, N, fmt, ##__VA_ARGS__)
#define ELOG_EVERY_N_NOTICE(N, fmt, ...) ELOG_EVERY_N(elog::ELEVEL_NOTICE, N, fmt, ##__VA_ARGS__)
#define ELOG_EVERY_N_INFO(N, fmt, ...) ELOG_EVERY_N(elog::ELEVEL_INFO, N, fmt, ##__VA_ARGS__)
#define ELOG_EVERY_N_TRACE(N, fmt, ...) ELOG_EVERY_N(elog::ELEVEL_TRACE, N, fmt, ##__VA_ARGS__)
#define ELOG_EVERY_N_DEBUG(N, fmt, ...) ELOG_EVERY_N(elog::ELEVEL_DEBUG, N, fmt, ##__VA_ARGS__)
#define ELOG_EVERY_N_DIAG(N, fmt, ...) ELOG_EVERY_N(elog::ELEVEL_DIAG, N, fmt, ##__VA_ARGS__)

/**************************************************************************************
 *                          fmtlib Moderate Logging Macros
 **************************************************************************************/

#ifdef ELOG_ENABLE_FMT_LIB
/**
 * @brief Logs a formatted message, once in every N calls (fmtlib style).
 * @param logger The logger used for message formatting.
 * @param level The log level. If the log level is insufficient, then the message is dropped.
 * @param N Specifies to log once in N log messages.
 * @param fmtStr The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_FMT_EVERY_N_EX(logger, level, N, fmtStr, ...)                \
    {                                                                     \
        elog::ELogLogger* validLogger = elog::getValidLogger(logger);     \
        if (validLogger->canLog(level)) {                                 \
            static std::atomic<uint64_t> count = 0;                       \
            if (count.fetch_add(1, std::memory_order_relaxed) % N == 0) { \
                ELOG_FMT_BASE(validLogger, level, fmtStr, ##__VA_ARGS__); \
            }                                                             \
        }                                                                 \
    }

// per-level every-N logging macros (fmtlib style)
#define ELOG_FMT_EVERY_N_FATAL_EX(logger, N, fmt, ...) \
    ELOG_FMT_EVERY_N_EX(logger, elog::ELEVEL_FATAL, N, fmt, ##__VA_ARGS__)
#define ELOG_FMT_EVERY_N_ERROR_EX(logger, N, fmt, ...) \
    ELOG_FMT_EVERY_N_EX(logger, elog::ELEVEL_ERROR, N, fmt, ##__VA_ARGS__)
#define ELOG_FMT_EVERY_N_WARN_EX(logger, N, fmt, ...) \
    ELOG_FMT_EVERY_N_EX(logger, elog::ELEVEL_WARN, N, fmt, ##__VA_ARGS__)
#define ELOG_FMT_EVERY_N_NOTICE_EX(logger, N, fmt, ...) \
    ELOG_FMT_EVERY_N_EX(logger, elog::ELEVEL_NOTICE, N, fmt, ##__VA_ARGS__)
#define ELOG_FMT_EVERY_N_INFO_EX(logger, N, fmt, ...) \
    ELOG_FMT_EVERY_N_EX(logger, elog::ELEVEL_INFO, N, fmt, ##__VA_ARGS__)
#define ELOG_FMT_EVERY_N_TRACE_EX(logger, N, fmt, ...) \
    ELOG_FMT_EVERY_N_EX(logger, elog::ELEVEL_TRACE, N, fmt, ##__VA_ARGS__)
#define ELOG_FMT_EVERY_N_DEBUG_EX(logger, N, fmt, ...) \
    ELOG_FMT_EVERY_N_EX(logger, elog::ELEVEL_DEBUG, N, fmt, ##__VA_ARGS__)
#define ELOG_FMT_EVERY_N_DIAG_EX(logger, N, fmt, ...) \
    ELOG_FMT_EVERY_N_EX(logger, elog::ELEVEL_DIAG, N, fmt, ##__VA_ARGS__)

// per-level every-N logging macros (fmtlib style, no logger)
#define ELOG_FMT_EVERY_N(level, N, fmt, ...) \
    ELOG_FMT_EVERY_N_EX(nullptr, level, N, fmt, ##__VA_ARGS__)

#define ELOG_FMT_EVERY_N_FATAL(N, fmt, ...) \
    ELOG_FMT_EVERY_N(elog::ELEVEL_FATAL, N, fmt, ##__VA_ARGS__)
#define ELOG_FMT_EVERY_N_ERROR(N, fmt, ...) \
    ELOG_FMT_EVERY_N(elog::ELEVEL_ERROR, N, fmt, ##__VA_ARGS__)
#define ELOG_FMT_EVERY_N_WARN(N, fmt, ...) \
    ELOG_FMT_EVERY_N(elog::ELEVEL_WARN, N, fmt, ##__VA_ARGS__)
#define ELOG_FMT_EVERY_N_NOTICE(N, fmt, ...) \
    ELOG_FMT_EVERY_N(elog::ELEVEL_NOTICE, N, fmt, ##__VA_ARGS__)
#define ELOG_FMT_EVERY_N_INFO(N, fmt, ...) \
    ELOG_FMT_EVERY_N(elog::ELEVEL_INFO, N, fmt, ##__VA_ARGS__)
#define ELOG_FMT_EVERY_N_TRACE(N, fmt, ...) \
    ELOG_FMT_EVERY_N(elog::ELEVEL_TRACE, N, fmt, ##__VA_ARGS__)
#define ELOG_FMT_EVERY_N_DEBUG(N, fmt, ...) \
    ELOG_FMT_EVERY_N(elog::ELEVEL_DEBUG, N, fmt, ##__VA_ARGS__)
#define ELOG_FMT_EVERY_N_DIAG(N, fmt, ...) \
    ELOG_FMT_EVERY_N(elog::ELEVEL_DIAG, N, fmt, ##__VA_ARGS__)
#endif

/**************************************************************************************
 *                          Binary Moderate Logging Macros
 **************************************************************************************/

#ifdef ELOG_ENABLE_FMT_LIB
/**
 * @brief Logs a formatted message, once in every N calls (fmtlib style, binary form).
 * @param logger The logger used for message formatting.
 * @param level The log level. If the log level is insufficient, then the message is dropped.
 * @param N Specifies to log once in N log messages.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_BIN_EVERY_N_EX(logger, level, N, fmt, ...)                   \
    {                                                                     \
        elog::ELogLogger* validLogger = elog::getValidLogger(logger);     \
        if (validLogger->canLog(level)) {                                 \
            static std::atomic<uint64_t> count = 0;                       \
            if (count.fetch_add(1, std::memory_order_relaxed) % N == 0) { \
                ELOG_BIN_BASE(validLogger, level, fmt, ##__VA_ARGS__);    \
            }                                                             \
        }                                                                 \
    }

// per-level every-N logging macros (fmtlib style, binary logging)
#define ELOG_BIN_EVERY_N_FATAL_EX(logger, N, fmt, ...) \
    ELOG_BIN_EVERY_N_EX(logger, elog::ELEVEL_FATAL, N, fmt, ##__VA_ARGS__)
#define ELOG_BIN_EVERY_N_ERROR_EX(logger, N, fmt, ...) \
    ELOG_BIN_EVERY_N_EX(logger, elog::ELEVEL_ERROR, N, fmt, ##__VA_ARGS__)
#define ELOG_BIN_EVERY_N_WARN_EX(logger, N, fmt, ...) \
    ELOG_BIN_EVERY_N_EX(logger, elog::ELEVEL_WARN, N, fmt, ##__VA_ARGS__)
#define ELOG_BIN_EVERY_N_NOTICE_EX(logger, N, fmt, ...) \
    ELOG_BIN_EVERY_N_EX(logger, elog::ELEVEL_NOTICE, N, fmt, ##__VA_ARGS__)
#define ELOG_BIN_EVERY_N_INFO_EX(logger, N, fmt, ...) \
    ELOG_BIN_EVERY_N_EX(logger, elog::ELEVEL_INFO, N, fmt, ##__VA_ARGS__)
#define ELOG_BIN_EVERY_N_TRACE_EX(logger, N, fmt, ...) \
    ELOG_BIN_EVERY_N_EX(logger, elog::ELEVEL_TRACE, N, fmt, ##__VA_ARGS__)
#define ELOG_BIN_EVERY_N_DEBUG_EX(logger, N, fmt, ...) \
    ELOG_BIN_EVERY_N_EX(logger, elog::ELEVEL_DEBUG, N, fmt, ##__VA_ARGS__)
#define ELOG_BIN_EVERY_N_DIAG_EX(logger, N, fmt, ...) \
    ELOG_BIN_EVERY_N_EX(logger, elog::ELEVEL_DIAG, N, fmt, ##__VA_ARGS__)

// per-level every-N logging macros (fmtlib style, binary logging, no logger)
#define ELOG_BIN_EVERY_N(level, N, fmt, ...) \
    ELOG_BIN_EVERY_N_EX(nullptr, level, N, fmt, ##__VA_ARGS__)

#define ELOG_BIN_EVERY_N_FATAL(N, fmt, ...) \
    ELOG_BIN_EVERY_N(elog::ELEVEL_FATAL, N, fmt, ##__VA_ARGS__)
#define ELOG_BIN_EVERY_N_ERROR(N, fmt, ...) \
    ELOG_BIN_EVERY_N(elog::ELEVEL_ERROR, N, fmt, ##__VA_ARGS__)
#define ELOG_BIN_EVERY_N_WARN(N, fmt, ...) \
    ELOG_BIN_EVERY_N(elog::ELEVEL_WARN, N, fmt, ##__VA_ARGS__)
#define ELOG_BIN_EVERY_N_NOTICE(N, fmt, ...) \
    ELOG_BIN_EVERY_N(elog::ELEVEL_NOTICE, N, fmt, ##__VA_ARGS__)
#define ELOG_BIN_EVERY_N_INFO(N, fmt, ...) \
    ELOG_BIN_EVERY_N(elog::ELEVEL_INFO, N, fmt, ##__VA_ARGS__)
#define ELOG_BIN_EVERY_N_TRACE(N, fmt, ...) \
    ELOG_BIN_EVERY_N(elog::ELEVEL_TRACE, N, fmt, ##__VA_ARGS__)
#define ELOG_BIN_EVERY_N_DEBUG(N, fmt, ...) \
    ELOG_BIN_EVERY_N(elog::ELEVEL_DEBUG, N, fmt, ##__VA_ARGS__)
#define ELOG_BIN_EVERY_N_DIAG(N, fmt, ...) \
    ELOG_BIN_EVERY_N(elog::ELEVEL_DIAG, N, fmt, ##__VA_ARGS__)
#endif

/**************************************************************************************
 *                          Auto-Cached Moderate Logging Macros
 **************************************************************************************/

#ifdef ELOG_ENABLE_FMT_LIB
/**
 * @brief Logs a formatted message, once in every N calls (fmtlib style, binary form, auto-cached).
 * @param logger The logger used for message formatting.
 * @param level The log level. If the log level is insufficient, then the message is dropped.
 * @param N Specifies to log once in N log messages.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_CACHE_EVERY_N_EX(logger, level, N, fmt, ...)                      \
    {                                                                          \
        elog::ELogLogger* validLogger = elog::getValidLogger(logger);          \
        if (validLogger->canLog(level)) {                                      \
            static thread_local elog::ELogCacheEntryId cacheEntryId =          \
                elog::getOrCacheFormatMsg(fmt);                                \
            static std::atomic<uint64_t> count = 0;                            \
            if (count.fetch_add(1, std::memory_order_relaxed) % N == 0) {      \
                ELOG_ID_BASE(validLogger, level, cacheEntryId, ##__VA_ARGS__); \
            }                                                                  \
        }                                                                      \
    }

// per-level every-N logging macros (fmtlib style, binary logging, auto-cached)
#define ELOG_CACHE_EVERY_N_FATAL_EX(logger, N, fmt, ...) \
    ELOG_CACHE_EVERY_N_EX(logger, elog::ELEVEL_FATAL, N, fmt, ##__VA_ARGS__)
#define ELOG_CACHE_EVERY_N_ERROR_EX(logger, N, fmt, ...) \
    ELOG_CACHE_EVERY_N_EX(logger, elog::ELEVEL_ERROR, N, fmt, ##__VA_ARGS__)
#define ELOG_CACHE_EVERY_N_WARN_EX(logger, N, fmt, ...) \
    ELOG_CACHE_EVERY_N_EX(logger, elog::ELEVEL_WARN, N, fmt, ##__VA_ARGS__)
#define ELOG_CACHE_EVERY_N_NOTICE_EX(logger, N, fmt, ...) \
    ELOG_CACHE_EVERY_N_EX(logger, elog::ELEVEL_NOTICE, N, fmt, ##__VA_ARGS__)
#define ELOG_CACHE_EVERY_N_INFO_EX(logger, N, fmt, ...) \
    ELOG_CACHE_EVERY_N_EX(logger, elog::ELEVEL_INFO, N, fmt, ##__VA_ARGS__)
#define ELOG_CACHE_EVERY_N_TRACE_EX(logger, N, fmt, ...) \
    ELOG_CACHE_EVERY_N_EX(logger, elog::ELEVEL_TRACE, N, fmt, ##__VA_ARGS__)
#define ELOG_CACHE_EVERY_N_DEBUG_EX(logger, N, fmt, ...) \
    ELOG_CACHE_EVERY_N_EX(logger, elog::ELEVEL_DEBUG, N, fmt, ##__VA_ARGS__)
#define ELOG_CACHE_EVERY_N_DIAG_EX(logger, N, fmt, ...) \
    ELOG_CACHE_EVERY_N_EX(logger, elog::ELEVEL_DIAG, N, fmt, ##__VA_ARGS__)

// per-level every-N logging macros (fmtlib style, binary logging, auto-cached, no logger
// specified)
#define ELOG_CACHE_EVERY_N(level, N, fmt, ...) \
    ELOG_CACHE_EVERY_N_EX(nullptr, level, N, fmt, ##__VA_ARGS__)

#define ELOG_CACHE_EVERY_N_FATAL(N, fmt, ...) \
    ELOG_CACHE_EVERY_N(elog::ELEVEL_FATAL, N, fmt, ##__VA_ARGS__)
#define ELOG_CACHE_EVERY_N_ERROR(N, fmt, ...) \
    ELOG_CACHE_EVERY_N(elog::ELEVEL_ERROR, N, fmt, ##__VA_ARGS__)
#define ELOG_CACHE_EVERY_N_WARN(N, fmt, ...) \
    ELOG_CACHE_EVERY_N(elog::ELEVEL_WARN, N, fmt, ##__VA_ARGS__)
#define ELOG_CACHE_EVERY_N_NOTICE(N, fmt, ...) \
    ELOG_CACHE_EVERY_N(elog::ELEVEL_NOTICE, N, fmt, ##__VA_ARGS__)
#define ELOG_CACHE_EVERY_N_INFO(N, fmt, ...) \
    ELOG_CACHE_EVERY_N(elog::ELEVEL_INFO, N, fmt, ##__VA_ARGS__)
#define ELOG_CACHE_EVERY_N_TRACE(N, fmt, ...) \
    ELOG_CACHE_EVERY_N(elog::ELEVEL_TRACE, N, fmt, ##__VA_ARGS__)
#define ELOG_CACHE_EVERY_N_DEBUG(N, fmt, ...) \
    ELOG_CACHE_EVERY_N(elog::ELEVEL_DEBUG, N, fmt, ##__VA_ARGS__)
#define ELOG_CACHE_EVERY_N_DIAG(N, fmt, ...) \
    ELOG_CACHE_EVERY_N(elog::ELEVEL_DIAG, N, fmt, ##__VA_ARGS__)
#endif

/**************************************************************************************
 *                          Pre-Cached Moderate Logging Macros
 **************************************************************************************/

#ifdef ELOG_ENABLE_FMT_LIB
/**
 * @brief Logs a formatted message, once in every N calls (fmtlib style, binary form, pre-cached).
 * @param logger The logger used for message formatting.
 * @param level The log level. If the log level is insufficient, then the message is dropped.
 * @param N Specifies to log once in N log messages.
 * @param cacheEntryId The cached log message format string id.
 * @param ... Log message format string parameters.
 */
#define ELOG_ID_EVERY_N_EX(logger, level, N, cacheEntryId, ...)                \
    {                                                                          \
        elog::ELogLogger* validLogger = elog::getValidLogger(logger);          \
        if (validLogger->canLog(level)) {                                      \
            static std::atomic<uint64_t> count = 0;                            \
            if (count.fetch_add(1, std::memory_order_relaxed) % N == 0) {      \
                ELOG_ID_BASE(validLogger, level, cacheEntryId, ##__VA_ARGS__); \
            }                                                                  \
        }                                                                      \
    }

// per-level every-N logging macros (fmtlib style, binary logging, pre-cached)
#define ELOG_ID_EVERY_N_FATAL_EX(logger, N, cacheEntryId, ...) \
    ELOG_ID_EVERY_N_EX(logger, elog::ELEVEL_FATAL, N, cacheEntryId, ##__VA_ARGS__)
#define ELOG_ID_EVERY_N_ERROR_EX(logger, N, cacheEntryId, ...) \
    ELOG_ID_EVERY_N_EX(logger, elog::ELEVEL_ERROR, N, cacheEntryId, ##__VA_ARGS__)
#define ELOG_ID_EVERY_N_WARN_EX(logger, N, cacheEntryId, ...) \
    ELOG_ID_EVERY_N_EX(logger, elog::ELEVEL_WARN, N, cacheEntryId, ##__VA_ARGS__)
#define ELOG_ID_EVERY_N_NOTICE_EX(logger, N, cacheEntryId, ...) \
    ELOG_ID_EVERY_N_EX(logger, elog::ELEVEL_NOTICE, N, cacheEntryId, ##__VA_ARGS__)
#define ELOG_ID_EVERY_N_INFO_EX(logger, N, cacheEntryId, ...) \
    ELOG_ID_EVERY_N_EX(logger, elog::ELEVEL_INFO, N, cacheEntryId, ##__VA_ARGS__)
#define ELOG_ID_EVERY_N_TRACE_EX(logger, N, cacheEntryId, ...) \
    ELOG_ID_EVERY_N_EX(logger, elog::ELEVEL_TRACE, N, cacheEntryId, ##__VA_ARGS__)
#define ELOG_ID_EVERY_N_DEBUG_EX(logger, N, cacheEntryId, ...) \
    ELOG_ID_EVERY_N_EX(logger, elog::ELEVEL_DEBUG, N, cacheEntryId, ##__VA_ARGS__)
#define ELOG_ID_EVERY_N_DIAG_EX(logger, N, cacheEntryId, ...) \
    ELOG_ID_EVERY_N_EX(logger, elog::ELEVEL_DIAG, N, cacheEntryId, ##__VA_ARGS__)

// per-level every-N logging macros (fmtlib style, binary logging, pre-cached, no logger)
#define ELOG_ID_EVERY_N(level, N, cacheEntryId, ...) \
    ELOG_ID_EVERY_N_EX(nullptr, level, N, cacheEntryId, ##__VA_ARGS__)

#define ELOG_ID_EVERY_N_FATAL(N, cacheEntryId, ...) \
    ELOG_ID_EVERY_N(elog::ELEVEL_FATAL, N, cacheEntryId, ##__VA_ARGS__)
#define ELOG_ID_EVERY_N_ERROR(N, cacheEntryId, ...) \
    ELOG_ID_EVERY_N(elog::ELEVEL_ERROR, N, cacheEntryId, ##__VA_ARGS__)
#define ELOG_ID_EVERY_N_WARN(N, cacheEntryId, ...) \
    ELOG_ID_EVERY_N(elog::ELEVEL_WARN, N, cacheEntryId, ##__VA_ARGS__)
#define ELOG_ID_EVERY_N_NOTICE(N, cacheEntryId, ...) \
    ELOG_ID_EVERY_N(elog::ELEVEL_NOTICE, N, cacheEntryId, ##__VA_ARGS__)
#define ELOG_ID_EVERY_N_INFO(N, cacheEntryId, ...) \
    ELOG_ID_EVERY_N(elog::ELEVEL_INFO, N, cacheEntryId, ##__VA_ARGS__)
#define ELOG_ID_EVERY_N_TRACE(N, cacheEntryId, ...) \
    ELOG_ID_EVERY_N(elog::ELEVEL_TRACE, N, cacheEntryId, ##__VA_ARGS__)
#define ELOG_ID_EVERY_N_DEBUG(N, cacheEntryId, ...) \
    ELOG_ID_EVERY_N(elog::ELEVEL_DEBUG, N, cacheEntryId, ##__VA_ARGS__)
#define ELOG_ID_EVERY_N_DIAG(N, cacheEntryId, ...) \
    ELOG_ID_EVERY_N(elog::ELEVEL_DIAG, N, cacheEntryId, ##__VA_ARGS__)
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

#endif  // __ELOG_API_H__