#include "elog_system.h"

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <unordered_map>

#include "elog_buffered_file_target.h"
#include "elog_common.h"
#include "elog_config_loader.h"
#include "elog_config_parser.h"
#include "elog_dbg_util_log_handler.h"
#include "elog_error.h"
#include "elog_field_selector_internal.h"
#include "elog_file_target.h"
#include "elog_filter_internal.h"
#include "elog_flush_policy.h"
#include "elog_flush_policy_internal.h"
#include "elog_level_cfg.h"
#include "elog_rate_limiter.h"
#include "elog_schema_manager.h"
#include "elog_segmented_file_target.h"
#include "elog_syslog_target.h"
#include "elog_target_spec.h"

namespace elog {

#ifdef ELOG_ENABLE_STACK_TRACE
static ELogDbgUtilLogHandler sDbgUtilLogHandler;
#endif

static ELogFilter* sGlobalFilter = nullptr;
static std::vector<ELogTarget*> sLogTargets;
static std::atomic<ELogSourceId> sNextLogSourceId;

inline ELogSourceId allocLogSourceId() {
    return sNextLogSourceId.fetch_add(1, std::memory_order_relaxed);
}

static std::mutex sSourceTreeLock;
static ELogSource* sRootLogSource = nullptr;
typedef std::unordered_map<ELogSourceId, ELogSource*> ELogSourceMap;
static ELogSourceMap sLogSourceMap;
static ELogLogger* sDefaultLogger = nullptr;
static ELogTarget* sDefaultLogTarget = nullptr;
static ELogFormatter* sGlobalFormatter = nullptr;

bool ELogSystem::initGlobals() {
    // init the error log so we can have trace messages as early as possible
    ELogError::initError();

    ELOG_REPORT_TRACE("Starting ELog initialization sequence");
    if (!initFieldSelectors()) {
        ELOG_REPORT_ERROR("Failed to initialize field selectors");
        return false;
    }
    ELOG_REPORT_TRACE("Field selectors initialized");

    if (!ELogSchemaManager::initSchemaHandlers()) {
        ELOG_REPORT_ERROR("Failed to initialize schema handlers");
        termGlobals();
        return false;
    }
    ELOG_REPORT_TRACE("Schema manager initialized");

    if (!initFlushPolicies()) {
        ELOG_REPORT_ERROR("Failed to initialize flush policies");
        termGlobals();
        return false;
    }
    ELOG_REPORT_TRACE("Flush policies initialized");

    if (!initFilters()) {
        ELOG_REPORT_ERROR("Failed to initialize filters");
        termGlobals();
        return false;
    }
    ELOG_REPORT_TRACE("Filters initialized");

    // root logger has no name
    // NOTE: this is the only place where we cannot use logging macros
    sRootLogSource = new (std::nothrow) ELogSource(allocLogSourceId(), "");
    if (sRootLogSource == nullptr) {
        ELOG_REPORT_ERROR("Failed to create root log source, out of memory");
        termGlobals();
        return false;
    }
    ELOG_REPORT_TRACE("Root log source initialized");

    // add to global map
    bool res =
        sLogSourceMap.insert(ELogSourceMap::value_type(sRootLogSource->getId(), sRootLogSource))
            .second;
    if (!res) {
        ELOG_REPORT_ERROR(
            "Failed to insert root log source to global source map (duplicate found)");
        termGlobals();
        return false;
    }
    ELOG_REPORT_TRACE("Root log source added to global log source map");

    sDefaultLogger = sRootLogSource->createSharedLogger();
    if (sDefaultLogger == nullptr) {
        ELOG_REPORT_ERROR("Failed to create default logger, out of memory");
        termGlobals();
        return false;
    }
    ELOG_REPORT_TRACE("Default logger initialized");

    sDefaultLogTarget = new (std::nothrow) ELogFileTarget(stderr);
    if (sDefaultLogTarget == nullptr) {
        ELOG_REPORT_ERROR("Failed to create default log target, out of memory");
        termGlobals();
        return false;
    }
    ELOG_REPORT_TRACE("Default log target initialized");

    sGlobalFormatter = new (std::nothrow) ELogFormatter();
    if (!sGlobalFormatter->initialize()) {
        ELOG_REPORT_ERROR("Failed to initialize log formatter");
        termGlobals();
        return false;
    }
    ELOG_REPORT_TRACE("Global formatter initialized");

#ifdef ELOG_ENABLE_STACK_TRACE
    // connect to debug util library
    dbgutil::DbgUtilErr rc = dbgutil::initDbgUtil(&sDbgUtilLogHandler, dbgutil::LS_INFO);
    if (rc != DBGUTIL_ERR_OK) {
        ELOG_REPORT_ERROR("Failed to initialize dbgutil library");
        termGlobals();
        return false;
    }
    sDbgUtilLogHandler.applyLogLevelCfg();
    ELOG_REPORT_TRACE("Debug utility library logging initialized");
#endif

    ELOG_REPORT_TRACE("ELog initialized successfully");
    return true;
}

void ELogSystem::termGlobals() {
    for (ELogTarget* logTarget : sLogTargets) {
        if (logTarget != nullptr) {
            logTarget->stop();
            delete logTarget;
        }
    }
    sLogTargets.clear();

#ifdef ELOG_ENABLE_STACK_TRACE
    dbgutil::DbgUtilErr rc = dbgutil::termDbgUtil();
    if (rc != DBGUTIL_ERR_OK) {
        // TODO: what now?
    }
#endif

    setLogFormatter(nullptr);
    setLogFilter(nullptr);
    if (sDefaultLogTarget != nullptr) {
        delete sDefaultLogTarget;
        sDefaultLogTarget = nullptr;
    }
    if (sRootLogSource != nullptr) {
        delete sRootLogSource;
        sRootLogSource = nullptr;
    }
    sDefaultLogger = nullptr;
    sLogSourceMap.clear();

    termFilters();
    termFlushPolicies();
    ELogSchemaManager::termSchemaHandlers();
    termFieldSelectors();
}

bool ELogSystem::initialize(ELogErrorHandler* errorHandler /* = nullptr */) {
    setErrorHandler(errorHandler);
    return initGlobals();
}

// TODO: refactor init code
bool ELogSystem::initializeLogFile(const char* logFilePath, uint32_t bufferSize /* = 0 */,
                                   bool useLock /* = false */,
                                   ELogErrorHandler* errorHandler /* = nullptr */,
                                   ELogFlushPolicy* flushPolicy /* = nullptr */,
                                   ELogFilter* logFilter /* = nullptr */,
                                   ELogFormatter* logFormatter /* = nullptr */) {
    setErrorHandler(errorHandler);
    if (!initGlobals()) {
        return false;
    }
    if (bufferSize > 0) {
        if (!setBufferedLogFileTarget(logFilePath, bufferSize, useLock, flushPolicy)) {
            termGlobals();
            return false;
        }
    } else {
        if (setLogFileTarget(logFilePath, flushPolicy) == ELOG_INVALID_TARGET_ID) {
            termGlobals();
            return false;
        }
    }

    if (logFilter != nullptr) {
        setLogFilter(logFilter);
    }
    if (logFormatter != nullptr) {
        setLogFormatter(logFormatter);
    }
    return true;
}

bool ELogSystem::initializeSegmentedLogFile(const char* logPath, const char* logName,
                                            uint32_t segmentLimitMB,
                                            ELogErrorHandler* errorHandler /* = nullptr */,
                                            ELogFlushPolicy* flushPolicy /* = nullptr */,
                                            ELogFilter* logFilter /* = nullptr */,
                                            ELogFormatter* logFormatter /* = nullptr */) {
    setErrorHandler(errorHandler);
    if (!initGlobals()) {
        return false;
    }
    if (setSegmentedLogFileTarget(logPath, logName, segmentLimitMB, flushPolicy) ==
        ELOG_INVALID_TARGET_ID) {
        termGlobals();
        return false;
    }

    if (logFilter != nullptr) {
        setLogFilter(logFilter);
    }
    if (logFormatter != nullptr) {
        setLogFormatter(logFormatter);
    }
    return true;
}

void ELogSystem::terminate() { termGlobals(); }

void ELogSystem::setErrorHandler(ELogErrorHandler* errorHandler) {
    ELogError::setErrorHandler(errorHandler);
}

void ELogSystem::setTraceMode(bool enableTrace /* = true */) {
    ELogError::setTraceMode(enableTrace);
}

bool ELogSystem::isTraceEnabled() { return ELogError::isTraceEnabled(); }

bool ELogSystem::registerSchemaHandler(const char* schemeName, ELogSchemaHandler* schemaHandler) {
    return ELogSchemaManager::registerSchemaHandler(schemeName, schemaHandler);
}

bool ELogSystem::configureRateLimit(const std::string& rateLimitCfg) {
    uint32_t maxMsgPerSec = 0;
    if (!parseIntProp("log_rate_limit", "", rateLimitCfg, maxMsgPerSec)) {
        ELOG_REPORT_ERROR("Failed to parse rate limit configuration: ", rateLimitCfg.c_str());
        return false;
    }
    return setRateLimit(maxMsgPerSec);
}

bool ELogSystem::configureLogTarget(const std::string& logTargetCfg) {
    // the following formats are currently supported as a URL-like string
    //
    // sys://stdout
    // sys://stderr
    // sys://syslog
    //
    // file://path
    // file://path?segment-size-mb=<segment-size-mb>
    //
    // optional parameters (each set is mutually exclusive with other sets)
    // defer (no value associated)
    // queue_batch_size=<batch-size>,queue_timeout_millis=<timeout-millis>
    // quantum_buffer_size=<buffer-size>
    //
    // future provision:
    // tcp://host:port
    // udp://host:port
    // db://db-name?conn_string=<conn-string>&insert-statement=<insert-statement>
    // msgq://message-broker-name?conn_string=<conn-string>&queue=<queue-name>&msgq_topic=<topic-name>
    //
    // additionally the following nested format is accepted:
    //
    // log_target = { scheme=db, db-name=postgresql, ...}
    // log_target = { scheme = async, type = deferred, log_target = { scheme = file, path = ...}}
    // log_target = { scheme = async, type = quantum, quantum_buffer_size = 10000,
    //      log_target = [{ scheme = file, path = ...}, {}, {}]}
    //
    // in theory nesting level is not restricted, but it doesn't make sense to have more than 2

    ELogTargetSpecStyle specStyle = ELogTargetSpecStyle::ELOG_STYLE_URL;
    ELogTargetNestedSpec logTargetNestedSpec;
    if (!ELogConfigParser::parseLogTargetSpec(logTargetCfg, logTargetNestedSpec, specStyle)) {
        return false;
    }

    // load the target (common properties already configured)
    ELogTarget* logTarget =
        ELogConfigLoader::loadLogTarget(logTargetCfg, logTargetNestedSpec, specStyle);
    if (logTarget == nullptr) {
        return false;
    }

    // finally add the log target
    if (addLogTarget(logTarget) == ELOG_INVALID_TARGET_ID) {
        ELOG_REPORT_ERROR("Failed to add log target for scheme %s: %s",
                          logTargetNestedSpec.m_spec.m_scheme.c_str(), logTargetCfg.c_str());
        delete logTarget;
        return false;
    }
    return true;
}

bool ELogSystem::configureFromFile(const char* configPath, bool defineLogSources /* = false */,
                                   bool defineMissingPath /* = false */) {
    // elog requires properties in order due to log level propagation
    ELogPropertySequence props;
    if (!ELogConfigLoader::loadFileProperties(configPath, props)) {
        return false;
    }
    return configureFromProperties(props, defineLogSources, defineMissingPath);
}

bool ELogSystem::configureFromProperties(const ELogPropertySequence& props,
                                         bool defineLogSources /* = false */,
                                         bool defineMissingPath /* = false */) {
    // configure log format (unrelated to order of appearance)
    // NOTE: only one such item is expected
    std::string logFormatCfg;
    if (getProp(props, "log_format", logFormatCfg)) {
        if (!configureLogFormat(logFormatCfg.c_str())) {
            ELOG_REPORT_ERROR("Invalid log format in properties: %s", logFormatCfg.c_str());
            return false;
        }
    }

    // configure global rate limit
    std::string rateLimitCfg;
    if (getProp(props, "log_rate_limit", rateLimitCfg)) {
        if (!configureRateLimit(rateLimitCfg)) {
            return false;
        }
    }

    std::vector<ELogLevelCfg> logLevelCfg;
    ELogLevel logLevel = ELEVEL_INFO;
    ELogPropagateMode propagateMode = ELogPropagateMode::PM_NONE;

    const char* logLevelSuffix1 = ".log_level";  // for configuration files
    const char* logLevelSuffix2 = "_log_level";  // for environment variables
    uint32_t logLevelSuffixLen = strlen(logLevelSuffix1);

    const char* logAffinitySuffix1 = ".log_affinity";  // for configuration files
    const char* logAffinitySuffix2 = "_log_affinity";  // for environment variables
    uint32_t logAffinitySuffixLen = strlen(logAffinitySuffix1);

    // get configuration also from env
    ELogPropertySequence envProps;
    char** envPtr = environ;
    for (; *envPtr; envPtr++) {
        std::string envVar(*envPtr);
        std::string::size_type equalPos = envVar.find('=');
        if (equalPos != std::string::npos) {
            std::string envVarName = envVar.substr(0, equalPos);
            std::string envVarValue = envVar.substr(equalPos + 1);
            if (envVarName.ends_with("log_level")) {
                ELOG_REPORT_TRACE("Adding prop %s = %s from env", envVarName.c_str(),
                                  envVarValue.c_str());
                envProps.push_back({envVarName, envVarValue});
            }
            if (envVarName.ends_with("log_affinity")) {
                ELOG_REPORT_TRACE("Adding prop %s = %s from env", envVarName.c_str(),
                                  envVarValue.c_str());
                envProps.push_back({envVarName, envVarValue});
            }
        }
    }

    // prepare combined properties, let env vars override property file
    ELogPropertySequence combinedProps;
    combinedProps.insert(combinedProps.end(), props.begin(), props.end());
    combinedProps.insert(combinedProps.end(), envProps.begin(), envProps.end());

    for (const ELogProperty& prop : combinedProps) {
        // check if this is root log level
        if (prop.first.compare("log_level") == 0) {
            // global log level
            if (!ELogConfigParser::parseLogLevel(prop.second.c_str(), logLevel, propagateMode)) {
                ELOG_REPORT_ERROR("Invalid global log level: %s", prop.second.c_str());
                return false;
            }
            logLevelCfg.push_back({getRootLogSource(), logLevel, propagateMode});
            continue;
        }

        // check for log target
        if (prop.first.compare("log_target") == 0) {
            // configure log target
            if (!configureLogTarget(prop.second)) {
                return false;
            }
            continue;
        }

        // configure log levels of log sources
        // search for ".log_level" with a dot, in order to filter out global log_level key
        // NOTE: when definining log sources, we must first define all log sources, then set log
        // level to configured level and apply log level propagation. If we apply propagation
        // before child log sources are defined, then propagation is lost.
        if (prop.first.ends_with(logLevelSuffix1) || prop.first.ends_with(logLevelSuffix2)) {
            const std::string& key = prop.first;
            // shave off trailing ".log_level/_log_level" (that includes dot/underscore)
            std::string sourceName = key.substr(0, key.size() - logLevelSuffixLen);
            ELogSource* logSource = defineLogSources
                                        ? defineLogSource(sourceName.c_str(), defineMissingPath)
                                        : getLogSource(sourceName.c_str());
            if (logSource == nullptr) {
                ELOG_REPORT_ERROR("Invalid log source name: %s", sourceName.c_str());
                return false;
            }
            propagateMode = ELogPropagateMode::PM_NONE;
            if (!ELogConfigParser::parseLogLevel(prop.second.c_str(), logLevel, propagateMode)) {
                ELOG_REPORT_ERROR("Invalid source %s log level: %s", sourceName.c_str(),
                                  prop.second.c_str());
                return false;
            }
            logLevelCfg.push_back({logSource, logLevel, propagateMode});
        }

        // configure log affinity of log sources
        if (prop.first.ends_with(logAffinitySuffix1) || prop.first.ends_with(logAffinitySuffix2)) {
            const std::string& key = prop.first;
            // shave off trailing ".log_level/_log_level" (that includes dot/underscore)
            std::string sourceName = key.substr(0, key.size() - logLevelSuffixLen);
            ELogSource* logSource = defineLogSources
                                        ? defineLogSource(sourceName.c_str(), defineMissingPath)
                                        : getLogSource(sourceName.c_str());
            if (logSource == nullptr) {
                ELOG_REPORT_ERROR("Invalid log source name: %s", sourceName.c_str());
                return false;
            }
            ELogTargetAffinityMask mask = 0;
            if (!ELogConfigParser::parseLogAffinityList(prop.second.c_str(), mask)) {
                ELOG_REPORT_ERROR("Invalid source %s log affinity specification: %s",
                                  sourceName.c_str(), prop.second.c_str());
                return false;
            }
            logSource->setLogTargetAffinity(mask);
        }
    }

    // now we can apply log level propagation
    for (const ELogLevelCfg& cfg : logLevelCfg) {
        ELOG_REPORT_TRACE("Setting %s log level to %s (propagate - %u)",
                          cfg.m_logSource->getQualifiedName(), elogLevelToStr(cfg.m_logLevel),
                          (uint32_t)cfg.m_propagationMode);
        cfg.m_logSource->setLogLevel(cfg.m_logLevel, cfg.m_propagationMode);
    }

    return true;
}

ELogTargetId ELogSystem::setLogTarget(ELogTarget* logTarget, bool printBanner /* = false */) {
    // first start the log target
    if (!logTarget->start()) {
        ELOG_REPORT_ERROR("Failed to start log target");
        return ELOG_INVALID_TARGET_ID;
    }

    // check if this is the first log target or not
    if (!sLogTargets.empty()) {
        for (ELogTarget* logTarget : sLogTargets) {
            if (logTarget != nullptr) {
                logTarget->stop();
            }
        }
        sLogTargets.clear();
    }

    sLogTargets.push_back(logTarget);
    if (printBanner) {
        ELOG_INFO("======================================================");
    }
    return (ELogTargetId)(sLogTargets.size() - 1);
}

ELogTargetId ELogSystem::setLogFileTarget(const char* logFilePath,
                                          ELogFlushPolicy* flushPolicy /* = nullptr */,
                                          bool printBanner /* = true */) {
    // create new log target
    ELogFileTarget* logTarget = new (std::nothrow) ELogFileTarget(logFilePath, flushPolicy);
    if (logTarget == nullptr) {
        ELOG_REPORT_ERROR("Failed to create log file target, out of memory");
        return ELOG_INVALID_TARGET_ID;
    }

    ELogTargetId logTargetId = setLogTarget(logTarget, printBanner);
    if (logTargetId == ELOG_INVALID_TARGET_ID) {
        delete logTarget;
    }
    return logTargetId;
}

ELogTargetId ELogSystem::setLogFileTarget(FILE* fileHandle,
                                          ELogFlushPolicy* flushPolicy /* = nullptr */,
                                          bool printBanner /* = false */) {
    // create new log target
    ELogFileTarget* logTarget = new (std::nothrow) ELogFileTarget(fileHandle, flushPolicy);
    if (logTarget == nullptr) {
        ELOG_REPORT_ERROR("Failed to create log target, out of memory");
        return ELOG_INVALID_TARGET_ID;
    }

    ELogTargetId logTargetId = setLogTarget(logTarget, printBanner);
    if (logTargetId == ELOG_INVALID_TARGET_ID) {
        delete logTarget;
    }
    return logTargetId;
}

ELogTargetId ELogSystem::setBufferedLogFileTarget(const char* logFilePath, uint32_t bufferSize,
                                                  bool useLock /* = true */,
                                                  ELogFlushPolicy* flushPolicy /* = nullptr */,
                                                  bool printBanner /* = false */) {
    // verify parameters
    if (bufferSize == 0) {
        ELOG_REPORT_ERROR("Invalid zero buffer size for buffered file log target");
        return ELOG_INVALID_TARGET_ID;
    }

    // create new log target
    ELogTarget* logTarget =
        new (std::nothrow) ELogBufferedFileTarget(logFilePath, bufferSize, useLock, flushPolicy);
    if (logTarget == nullptr) {
        ELOG_REPORT_ERROR("Failed to create log file target, out of memory");
        return ELOG_INVALID_TARGET_ID;
    }

    ELogTargetId logTargetId = setLogTarget(logTarget, printBanner);
    if (logTargetId == ELOG_INVALID_TARGET_ID) {
        delete logTarget;
    }
    return logTargetId;
}

ELogTargetId ELogSystem::setBufferedLogFileTarget(FILE* fileHandle, uint32_t bufferSize,
                                                  bool useLock /* = true */,
                                                  ELogFlushPolicy* flushPolicy /* = nullptr */,
                                                  bool printBanner /* = false */) {
    // verify parameters
    if (bufferSize == 0) {
        ELOG_REPORT_ERROR("Invalid zero buffer size for buffered file log target");
        return ELOG_INVALID_TARGET_ID;
    }

    // create new log target
    ELogTarget* logTarget =
        new (std::nothrow) ELogBufferedFileTarget(fileHandle, bufferSize, useLock, flushPolicy);
    if (logTarget == nullptr) {
        ELOG_REPORT_ERROR("Failed to create log file target, out of memory");
        return ELOG_INVALID_TARGET_ID;
    }

    ELogTargetId logTargetId = setLogTarget(logTarget, printBanner);
    if (logTargetId == ELOG_INVALID_TARGET_ID) {
        delete logTarget;
    }
    return logTargetId;
}

ELogTargetId ELogSystem::setSegmentedLogFileTarget(const char* logPath, const char* logName,
                                                   uint32_t segmentLimitMB,
                                                   ELogFlushPolicy* flushPolicy /* = nullptr */,
                                                   bool printBanner /* = true */) {
    // create new log target
    ELogTarget* logTarget =
        new (std::nothrow) ELogSegmentedFileTarget(logPath, logName, segmentLimitMB, flushPolicy);
    if (logTarget == nullptr) {
        ELOG_REPORT_ERROR("Failed to create segmented log file target, out of memory");
        return ELOG_INVALID_TARGET_ID;
    }

    ELogTargetId logTargetId = setLogTarget(logTarget, printBanner);
    if (logTargetId == ELOG_INVALID_TARGET_ID) {
        delete logTarget;
    }
    return logTargetId;
}

ELogTargetId ELogSystem::addLogTarget(ELogTarget* logTarget) {
    // TODO: should we guard against duplicate names (they are used in search by name and in log
    // affinity mask building) - this will require API change (returning at least bool)
    ELOG_REPORT_TRACE("Adding log target: %s", logTarget->getName());
    if (!logTarget->start()) {
        ELOG_REPORT_ERROR("Failed to start log target %s", logTarget->getName());
        return ELOG_INVALID_TARGET_ID;
    }

    // find vacant slot ro add a new one
    for (uint32_t i = 0; i < sLogTargets.size(); ++i) {
        if (sLogTargets[i] == nullptr) {
            sLogTargets[i] = logTarget;
            ELOG_REPORT_TRACE("Added log target %s with id %u", logTarget->getName(), i);
            return i;
        }
    }

    // otherwise add a new slot
    sLogTargets.push_back(logTarget);
    ELOG_REPORT_TRACE("Added log target %s with id %u", logTarget->getName(), sLogTargets.size());
    return (ELogTargetId)(sLogTargets.size() - 1);
}

ELogTargetId ELogSystem::addLogFileTarget(const char* logFilePath, uint32_t bufferSize /* = 0 */,
                                          bool useLock /* = false */,
                                          ELogFlushPolicy* flushPolicy /* = nullptr */) {
    // create new log target
    ELogTarget* logTarget = nullptr;
    if (bufferSize > 0) {
        logTarget = new (std::nothrow)
            ELogBufferedFileTarget(logFilePath, bufferSize, useLock, flushPolicy);
    } else {
        logTarget = new (std::nothrow) ELogFileTarget(logFilePath, flushPolicy);
    }
    if (logTarget == nullptr) {
        ELOG_REPORT_ERROR("Failed to create log target, out of memory");
        return ELOG_INVALID_TARGET_ID;
    }

    ELogTargetId logTargetId = addLogTarget(logTarget);
    if (logTargetId == ELOG_INVALID_TARGET_ID) {
        delete logTarget;
    }
    return logTargetId;
}

ELogTargetId ELogSystem::addLogFileTarget(FILE* fileHandle, uint32_t bufferSize /* = 0 */,
                                          bool useLock /* = false */,
                                          ELogFlushPolicy* flushPolicy /* = nullptr */) {
    ELogTarget* logTarget = nullptr;
    if (bufferSize > 0) {
        logTarget =
            new (std::nothrow) ELogBufferedFileTarget(fileHandle, bufferSize, useLock, flushPolicy);
    } else {
        logTarget = new (std::nothrow) ELogFileTarget(fileHandle, flushPolicy);
    }
    if (logTarget == nullptr) {
        ELOG_REPORT_ERROR("Failed to create log target, out of memory");
        return ELOG_INVALID_TARGET_ID;
    }

    ELogTargetId logTargetId = addLogTarget(logTarget);
    if (logTargetId == ELOG_INVALID_TARGET_ID) {
        delete logTarget;
    }
    return logTargetId;
}

ELogTargetId ELogSystem::addBufferedLogFileTarget(const char* logFilePath, uint32_t bufferSize,
                                                  bool useLock /* = true */,
                                                  ELogFlushPolicy* flushPolicy /* = nullptr */) {
    // verify parameters
    if (bufferSize == 0) {
        ELOG_REPORT_ERROR("Invalid zero buffer size for buffered file log target");
        return ELOG_INVALID_TARGET_ID;
    }

    // create new log target
    ELogTarget* logTarget =
        new (std::nothrow) ELogBufferedFileTarget(logFilePath, bufferSize, useLock, flushPolicy);
    if (logTarget == nullptr) {
        ELOG_REPORT_ERROR("Failed to create log file target, out of memory");
        return ELOG_INVALID_TARGET_ID;
    }

    ELogTargetId logTargetId = addLogTarget(logTarget);
    if (logTargetId == ELOG_INVALID_TARGET_ID) {
        delete logTarget;
    }
    return logTargetId;
}

ELogTargetId ELogSystem::addBufferedLogFileTarget(FILE* fileHandle, uint32_t bufferSize,
                                                  bool useLock /* = true */,
                                                  ELogFlushPolicy* flushPolicy /* = nullptr */) {
    // verify parameters
    if (bufferSize == 0) {
        ELOG_REPORT_ERROR("Invalid zero buffer size for buffered file log target");
        return ELOG_INVALID_TARGET_ID;
    }

    // create new log target
    ELogTarget* logTarget =
        new (std::nothrow) ELogBufferedFileTarget(fileHandle, bufferSize, useLock, flushPolicy);
    if (logTarget == nullptr) {
        ELOG_REPORT_ERROR("Failed to create log file target, out of memory");
        return ELOG_INVALID_TARGET_ID;
    }

    ELogTargetId logTargetId = addLogTarget(logTarget);
    if (logTargetId == ELOG_INVALID_TARGET_ID) {
        delete logTarget;
    }
    return logTargetId;
}

ELogTargetId ELogSystem::addSegmentedLogFileTarget(const char* logPath, const char* logName,
                                                   uint32_t segmentLimitMB,
                                                   ELogFlushPolicy* flushPolicy /* = nullptr */) {
    // create new log target
    ELogTarget* logTarget =
        new (std::nothrow) ELogSegmentedFileTarget(logPath, logName, segmentLimitMB, flushPolicy);
    if (logTarget == nullptr) {
        ELOG_REPORT_ERROR("Failed to create segmented log file target, out of memory");
        return ELOG_INVALID_TARGET_ID;
    }

    ELogTargetId logTargetId = addLogTarget(logTarget);
    if (logTargetId == ELOG_INVALID_TARGET_ID) {
        delete logTarget;
    }
    return logTargetId;
}

ELogTargetId ELogSystem::addStdErrLogTarget() { return addLogFileTarget(stderr); }

ELogTargetId ELogSystem::addStdOutLogTarget() { return addLogFileTarget(stdout); }

ELogTargetId ELogSystem::addSysLogTarget() {
#ifdef ELOG_LINUX
    ELogSysLogTarget* logTarget = new (std::nothrow) ELogSysLogTarget();
    return addLogTarget(logTarget);
#else
    return ELOG_INVALID_TARGET_ID;
#endif
}

ELogTarget* ELogSystem::getLogTarget(ELogTargetId targetId) {
    if (targetId >= sLogTargets.size()) {
        return nullptr;
    }
    return sLogTargets[targetId];
}

ELogTarget* ELogSystem::getLogTarget(const char* logTargetName) {
    for (ELogTarget* logTarget : sLogTargets) {
        if (strcmp(logTarget->getName(), logTargetName) == 0) {
            return logTarget;
        }
    }
    return nullptr;
}

ELogTargetId ELogSystem::getLogTargetId(const char* logTargetName) {
    for (uint32_t logTargetId = 0; logTargetId < sLogTargets.size(); ++logTargetId) {
        ELogTarget* logTarget = sLogTargets[logTargetId];
        if (strcmp(logTarget->getName(), logTargetName) == 0) {
            return logTargetId;
        }
    }
    return ELOG_INVALID_TARGET_ID;
}

void ELogSystem::removeLogTarget(ELogTargetId targetId) {
    // be careful, if this is the last log target, we must put back stderr
    if (targetId >= sLogTargets.size()) {
        // silently ignore invalid request
        return;
    }

    if (sLogTargets[targetId] == nullptr) {
        // silently ignore repeated request
        return;
    }

    // delete the log target and put null
    // we cannot shrink the vector because that will change log target indices
    sLogTargets[targetId]->stop();
    delete sLogTargets[targetId];
    sLogTargets[targetId] = nullptr;

    // find largest suffix of removed log targets
    int lastLogTarget = -1;
    for (int i = sLogTargets.size() - 1; i >= 0; --i) {
        if (sLogTargets[targetId] != nullptr) {
            // at least one log target active, so we can return
            lastLogTarget = i;
            break;
        }
    }

    // remove unused suffix
    sLogTargets.resize(lastLogTarget + 1);
}

void ELogSystem::removeLogTarget(ELogTarget* target) {
    // find log target and remove it
    for (uint32_t i = 0; i < sLogTargets.size(); ++i) {
        if (sLogTargets[i] == target) {
            removeLogTarget(i);
        }
    }
}

static void parseSourceName(const std::string& qualifiedName, std::vector<std::string>& namePath) {
    std::string::size_type prevDotPos = 0;
    std::string::size_type dotPos = qualifiedName.find('.');
    while (dotPos != std::string::npos) {
        if (dotPos > prevDotPos) {
            namePath.push_back(qualifiedName.substr(prevDotPos, dotPos - prevDotPos));
        }
        prevDotPos = dotPos + 1;
        dotPos = qualifiedName.find('.', prevDotPos);
    }
    if (prevDotPos < qualifiedName.length()) {
        namePath.push_back(qualifiedName.substr(prevDotPos));
    }
}

ELogSource* ELogSystem::addChildSource(ELogSource* parent, const char* sourceName) {
    ELogSource* logSource = new (std::nothrow) ELogSource(allocLogSourceId(), sourceName, parent);
    if (!parent->addChild(logSource)) {
        // impossible
        ELOG_REPORT_ERROR("Internal error, cannot add child source %s, already exists", sourceName);
        delete logSource;
        return nullptr;
    }

    bool res =
        sLogSourceMap.insert(ELogSourceMap::value_type(logSource->getId(), logSource)).second;
    if (!res) {
        // internal error, roll back
        ELOG_REPORT_ERROR("Internal error, cannot add new log source %s by id %u, already exists",
                          sourceName, logSource->getId());
        parent->removeChild(logSource->getName());
        delete logSource;
        return nullptr;
    }
    return logSource;
}

// log sources
ELogSource* ELogSystem::defineLogSource(const char* qualifiedName,
                                        bool defineMissingPath /* = false */) {
    std::unique_lock<std::mutex> lock(sSourceTreeLock);
    // parse name to components and start traveling up to last component
    std::vector<std::string> namePath;
    parseSourceName(qualifiedName, namePath);

    ELogSource* currSource = sRootLogSource;
    uint32_t namecount = namePath.size();
    for (uint32_t i = 0; i < namecount - 1; ++i) {
        ELogSource* childSource = currSource->getChild(namePath[i].c_str());
        if (childSource == nullptr && defineMissingPath) {
            childSource = addChildSource(currSource, namePath[i].c_str());
        }
        if (childSource == nullptr) {
            // TODO: partial failures are left as dangling sources, is that ok?
            return nullptr;
        }
        currSource = childSource;
    }

    // make sure name does not exist already
    const char* logSourceName = namePath.back().c_str();
    ELogSource* logSource = currSource->getChild(logSourceName);
    if (logSource != nullptr) {
        return logSource;
    }

    // otherwise create it and add it
    logSource = addChildSource(currSource, logSourceName);

    // in case of a new log source, we check if there is an environment variable for configuring
    // its log level. The expected format is: <qualified-log-source-name>_log_level =
    // <elog-level> every dot in the qualified name is replaced with underscore
    std::string envVarName = std::string(qualifiedName) + "_log_level";
    std::replace(envVarName.begin(), envVarName.end(), '.', '_');
    char* envVarValue = getenv(envVarName.c_str());
    if (envVarValue != nullptr) {
        ELogLevel logLevel = ELEVEL_INFO;
        if (elogLevelFromStr(envVarValue, logLevel)) {
            logSource->setLogLevel(logLevel, ELogPropagateMode::PM_NONE);
        }
    }

    return logSource;
}

ELogSource* ELogSystem::getLogSource(const char* qualifiedName) {
    std::unique_lock<std::mutex> lock(sSourceTreeLock);
    std::vector<std::string> namePath;
    parseSourceName(qualifiedName, namePath);
    ELogSource* currSource = sRootLogSource;
    uint32_t namecount = namePath.size();
    for (uint32_t i = 0; i < namecount; ++i) {
        currSource = currSource->getChild(namePath[i].c_str());
        if (currSource == nullptr) {
            break;
        }
    }
    return currSource;
}

ELogSource* ELogSystem::getLogSource(ELogSourceId logSourceId) {
    ELogSource* logSource = nullptr;
    std::unique_lock<std::mutex> lock(sSourceTreeLock);
    ELogSourceMap::iterator itr = sLogSourceMap.find(logSourceId);
    if (itr != sLogSourceMap.end()) {
        logSource = itr->second;
    }
    return logSource;
}

ELogSource* ELogSystem::getRootLogSource() { return sRootLogSource; }

// void configureLogSourceLevel(const char* qualifiedName, ELogLevel logLevel);

// logger interface
ELogLogger* ELogSystem::getDefaultLogger() { return sDefaultLogger; }

ELogLogger* ELogSystem::getSharedLogger(const char* qualifiedSourceName) {
    ELogLogger* logger = nullptr;
    ELogSource* source = getLogSource(qualifiedSourceName);
    if (source != nullptr) {
        logger = source->createSharedLogger();
    }
    return logger;
}

ELogLogger* ELogSystem::getPrivateLogger(const char* qualifiedSourceName) {
    ELogLogger* logger = nullptr;
    ELogSource* source = getLogSource(qualifiedSourceName);
    if (source != nullptr) {
        logger = source->createPrivateLogger();
    }
    return logger;
}

// ELogLogger* ELogSystem::getMultiThreadedLogger(const char* sourceName);
// ELogLogger* ELogSystem::getSingleThreadedLogger(const char* sourceName);

// log formatting
bool ELogSystem::configureLogFormat(const char* logFormat) {
    ELogFormatter* logFormatter = new (std::nothrow) ELogFormatter();
    if (!logFormatter->initialize(logFormat)) {
        delete logFormatter;
        return false;
    }
    setLogFormatter(logFormatter);
    return true;
}

void ELogSystem::setLogFormatter(ELogFormatter* logFormatter) {
    if (sGlobalFormatter != nullptr) {
        delete sGlobalFormatter;
    }
    sGlobalFormatter = logFormatter;
}

void ELogSystem::formatLogMsg(const ELogRecord& logRecord, std::string& logMsg) {
    sGlobalFormatter->formatLogMsg(logRecord, logMsg);
}

void ELogSystem::setCurrentThreadName(const char* threadName) {
    setCurrentThreadNameField(threadName);
}

// global log filtering
void ELogSystem::setLogFilter(ELogFilter* logFilter) {
    if (sGlobalFilter != nullptr) {
        delete sGlobalFilter;
    }
    sGlobalFilter = logFilter;
}

bool ELogSystem::setRateLimit(uint32_t maxMsgPerSecond) {
    ELogRateLimiter* rateLimiter = new (std::nothrow) ELogRateLimiter(maxMsgPerSecond);
    if (rateLimiter == nullptr) {
        ELOG_REPORT_ERROR("Failed to set rate limit, out of memory");
        return false;
    }
    setLogFilter(rateLimiter);
    return true;
}

bool ELogSystem::filterLogMsg(const ELogRecord& logRecord) {
    bool res = true;
    if (sGlobalFilter != nullptr) {
        res = sGlobalFilter->filterLogRecord(logRecord);
    }
    return res;
}

#ifdef ELOG_ENABLE_STACK_TRACE
/** @brief Stack entry printer to log. */
class LogStackEntryPrinter : public dbgutil::StackEntryPrinter {
public:
    LogStackEntryPrinter(ELogLevel logLevel, const char* title)
        : m_logLevel(logLevel), m_title(title) {}
    void onBeginStackTrace(dbgutil::os_thread_id_t threadId) override {
        // std::string threadName;
        // getThreadName(threadId, threadName);
        std::string threadName = getThreadNameField(threadId);
        if (m_title.empty()) {
            if (threadName.empty()) {
                ELOG_BEGIN(m_logLevel, "[Thread %" PRItid " (0x%" PRItidx ") stack trace]\n",
                           threadId, threadId);
            } else {
                ELOG_BEGIN(m_logLevel, "[Thread %" PRItid " (0x%" PRItidx ") <%s> stack trace]\n",
                           threadId, threadId, threadName.c_str());
            }
        } else {
            if (threadName.empty()) {
                ELOG_BEGIN(m_logLevel, "%s:\n[Thread %" PRItid " (0x%" PRItidx ") stack trace]\n",
                           m_title.c_str(), threadId, threadId);
            } else {
                ELOG_BEGIN(m_logLevel,
                           "%s:\n[Thread %" PRItid " (0x%" PRItidx ") <%s> stack trace]\n",
                           m_title.c_str(), threadId, threadId, threadName.c_str());
            }
        }
    }
    void onEndStackTrace() override { ELOG_END(); }
    void onStackEntry(const char* stackEntry) { ELOG_APPEND("%s\n", stackEntry); }

private:
    std::string m_title;
    ELogLevel m_logLevel;
};

void ELogSystem::logStackTrace(ELogLevel logLevel, const char* title, int skip,
                               dbgutil::StackEntryFormatter* formatter) {
    LogStackEntryPrinter printer(logLevel, title == nullptr ? "" : title);
    dbgutil::printStackTrace(skip, &printer, formatter);
}

void ELogSystem::logStackTraceContext(void* context, ELogLevel logLevel, const char* title,
                                      int skip, dbgutil::StackEntryFormatter* formatter) {
    LogStackEntryPrinter printer(logLevel, title == nullptr ? "" : title);
    dbgutil::printStackTraceContext(context, skip, &printer, formatter);
}

void ELogSystem::logAppStackTrace(ELogLevel logLevel, const char* title, int skip,
                                  dbgutil::StackEntryFormatter* formatter) {
    LogStackEntryPrinter printer(logLevel, title == nullptr ? "" : title);
    dbgutil::printAppStackTrace(skip, &printer, formatter);
}
#endif

void ELogSystem::log(
    const ELogRecord& logRecord,
    ELogTargetAffinityMask logTargetAffinityMask /* = ELOG_ALL_TARGET_AFFINITY_MASK */) {
    bool logged = false;
    for (ELogTargetId logTargetId = 0; logTargetId < sLogTargets.size(); ++logTargetId) {
        ELogTarget* logTarget = sLogTargets[logTargetId];
        if (logTargetId > ELOG_MAX_LOG_TARGET_ID_AFFINITY ||
            ELOG_HAS_TARGET_AFFINITY_MASK(logTargetAffinityMask, logTargetId + 1)) {
            logTarget->log(logRecord);
            logged = true;
        }
    }

    // by default, if no log target is defined yet, log is redirected to stderr
    if (!logged) {
        if (sDefaultLogTarget != nullptr) {
            sDefaultLogTarget->log(logRecord);
        } else {
            fprintf(stderr, "%s\n", logRecord.m_logMsg);
        }
    }
}

char* ELogSystem::sysErrorToStr(int sysErrorCode) { return ELogError::sysErrorToStr(sysErrorCode); }

#ifdef ELOG_WINDOWS
char* ELogSystem::win32SysErrorToStr(unsigned long sysErrorCode) {
    return ELogError::win32SysErrorToStr(sysErrorCode);
}

void ELogSystem::win32FreeErrorStr(char* errStr) { ELogError::win32FreeErrorStr(errStr); }
#endif

}  // namespace elog
