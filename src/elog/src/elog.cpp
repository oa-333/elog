#include "elog.h"

#include <algorithm>
#include <atomic>
#include <cinttypes>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <unordered_map>

#include "elog_buffered_file_target.h"
#include "elog_cache.h"
#include "elog_common.h"
#include "elog_config.h"
#include "elog_config_loader.h"
#include "elog_config_parser.h"
#include "elog_dbg_util_log_handler.h"
#include "elog_field_selector_internal.h"
#include "elog_file_schema_handler.h"
#include "elog_file_target.h"
#include "elog_filter_internal.h"
#include "elog_flush_policy.h"
#include "elog_flush_policy_internal.h"
#include "elog_level_cfg.h"
#include "elog_pre_init_logger.h"
#include "elog_rate_limiter.h"
#include "elog_report.h"
#include "elog_schema_manager.h"
#include "elog_segmented_file_target.h"
#include "elog_shared_logger.h"
#include "elog_stack_trace.h"
#include "elog_stats_internal.h"
#include "elog_syslog_target.h"
#include "elog_target_spec.h"
#include "elog_time_internal.h"
#include "elog_win32_event_log_target.h"

// #include "elog_props_formatter.h"
namespace elog {

#define ELOG_MAX_TARGET_COUNT 256ul

#ifdef ELOG_ENABLE_STACK_TRACE
static ELogDbgUtilLogHandler sDbgUtilLogHandler;
static bool sDbgUtilInitialized = false;
#endif

static bool sInitialized = false;
static bool sIsTerminating = false;
static uint32_t sMaxThreads = ELOG_DEFAULT_MAX_THREADS;
static ELogPreInitLogger sPreInitLogger;
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

// these functions are defined as external because they are friends of some classes
extern bool initGlobals();
extern void termGlobals();
static void setReportLevelFromEnv();
extern ELogSource* createLogSource(ELogSourceId sourceId, const char* name,
                                   ELogSource* parent = nullptr, ELogLevel logLevel = ELEVEL_INFO);
extern void deleteLogSource(ELogSource* logSource);

// local helpers
static ELogSource* addChildSource(ELogSource* parent, const char* sourceName);
static bool configureRateLimit(const std::string& rateLimitCfg);
static bool configureLogTargetImpl(const std::string& logTargetCfg, ELogTargetId* id = nullptr);
static bool configureLogTargetNode(const ELogConfigMapNode* logTargetCfg,
                                   ELogTargetId* id = nullptr);
static bool augmentConfigFromEnv(ELogConfigMapNode* cfgMap);

bool initGlobals() {
    // allow elog tracing early as possible
    setReportLevelFromEnv();

    // initialize the date table
    if (!initDateTable()) {
        ELOG_REPORT_ERROR("Failed to initialize date table");
        termGlobals();
        return false;
    }
    ELOG_REPORT_TRACE("Date table initialized");

    // initialize log target statistics
    if (!initializeStats(sMaxThreads)) {
        ELOG_REPORT_ERROR("Failed to initialize log target statistics");
        termGlobals();
        return false;
    }
    ELOG_REPORT_TRACE("Log target statistics initialized");

    // create thread local storage key for log buffers
    if (!ELogTarget::createLogBufferKey()) {
        ELOG_REPORT_ERROR("Failed to initialize log buffer thread local storage");
        termGlobals();
        return false;
    }
    ELOG_REPORT_TRACE("Log buffer TLS key initialized");

    // create thread local storage key for log buffers
    if (!ELogSharedLogger::createRecordBuilderKey()) {
        ELOG_REPORT_ERROR("Failed to initialize record builder thread local storage");
        termGlobals();
        return false;
    }
    ELOG_REPORT_TRACE("Record builder TLS key initialized");

    ELOG_REPORT_TRACE("Starting ELog initialization sequence");
    if (!initFieldSelectors()) {
        ELOG_REPORT_ERROR("Failed to initialize field selectors");
        termGlobals();
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
    sRootLogSource = createLogSource(allocLogSourceId(), "");
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

    // TODO: support also bin logger that does NOT format on client side, but rather using template
    // variadic args, encodes each parameter type with 1 byte, followed by parameter flat data, then
    // finally by 0xFF byte denoting start of format string, then followed by 4 bytes string len and
    // then followed by format string. Entire buffer may have some small header (total len, etc.).
    // actual formatting will take place later, either by async thread, or by external process.
    // need to define log target binary/text mode, so that if a text mode log target receives a
    // binary buffer, it must format it first. for a binary log target, the log record must also be
    // written in binary form.
    sDefaultLogger = sRootLogSource->createSharedLogger();
    if (sDefaultLogger == nullptr) {
        ELOG_REPORT_ERROR("Failed to create default logger, out of memory");
        termGlobals();
        return false;
    }
    ELOG_REPORT_TRACE("Default logger initialized");

    // NOTE: statistics disabled
    sDefaultLogTarget = new (std::nothrow) ELogFileTarget(stderr, nullptr, false);
    if (sDefaultLogTarget == nullptr) {
        ELOG_REPORT_ERROR("Failed to create default log target, out of memory");
        termGlobals();
        return false;
    }
    sDefaultLogTarget->setName("elog_default");
    if (!sDefaultLogTarget->start()) {
        ELOG_REPORT_ERROR("Failed to create default log target, out of memory");
        delete sDefaultLogTarget;
        sDefaultLogTarget = nullptr;
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

    // format message cache
    if (!ELogCache::initCache(ELOG_DEFAULT_CACHE_SIZE)) {
        ELOG_REPORT_ERROR("Failed to initialize format message cache");
        termGlobals();
        return false;
    }
    ELOG_REPORT_TRACE("Format message cache initialized");

#ifdef ELOG_ENABLE_STACK_TRACE
    // connect to debug util library
    dbgutil::DbgUtilErr rc =
        dbgutil::initDbgUtil(nullptr, &sDbgUtilLogHandler, dbgutil::LS_INFO, DBGUTIL_FLAGS_ALL);
    if (rc != DBGUTIL_ERR_OK) {
        ELOG_REPORT_ERROR("Failed to initialize dbgutil library");
        termGlobals();
        return false;
    }
    sDbgUtilLogHandler.applyLogLevelCfg();
    initStackTrace();
    ELOG_REPORT_TRACE("Debug utility library logging initialized");
    sDbgUtilInitialized = true;
#endif

    // now we can enable elog's self logger
    // any error up until now will be printed into stderr with no special formatting
    ELOG_REPORT_TRACE("Setting up ELog internal logger");
    ELogReport::initReport();

    ELOG_REPORT_INFO("ELog initialized successfully");
    return true;
}

void termGlobals() {
    sIsTerminating = true;
    clearAllLogTargets();
    ELogReport::termReport();

#ifdef ELOG_ENABLE_STACK_TRACE
    if (sDbgUtilInitialized) {
        dbgutil::DbgUtilErr rc = dbgutil::termDbgUtil();
        if (rc != DBGUTIL_ERR_OK) {
            // issue error and continue
            ELOG_REPORT_ERROR("Failed to terminate Debug Util library");
        }
        sDbgUtilInitialized = false;
    }
#endif

    ELogCache::destroyCache();
    setLogFormatter(nullptr);
    setLogFilter(nullptr);
    if (sDefaultLogTarget != nullptr) {
        sDefaultLogTarget->stop();
        delete sDefaultLogTarget;
        sDefaultLogTarget = nullptr;
    }
    if (sRootLogSource != nullptr) {
        deleteLogSource(sRootLogSource);
        sRootLogSource = nullptr;
    }
    sDefaultLogger = nullptr;
    sLogSourceMap.clear();

    termFilters();
    termFlushPolicies();
    ELogSchemaManager::termSchemaHandlers();
    termFieldSelectors();
    if (!ELogSharedLogger::destroyRecordBuilderKey()) {
        ELOG_REPORT_ERROR("Failed to destroy record builder thread-local storage");
    }
    terminateStats();
    if (!ELogTarget::destroyLogBufferKey()) {
        ELOG_REPORT_ERROR("Failed to destroy log buffer thread-local storage");
    }
    termDateTable();
    sPreInitLogger.discardAccumulatedLogMessages();
}

bool initialize(const char* configFile /* = nullptr */, uint32_t reloadPeriodMillis /* = 0 */,
                ELogReportHandler* elogReportHandler /* = nullptr */,
                ELogLevel elogReportLevel /* = ELEVEL_WARN */,
                uint32_t maxThreads /* = ELOG_DEFAULT_MAX_THREADS */) {
    if (sInitialized) {
        ELOG_REPORT_ERROR("Duplicate attempt to initialize rejected");
        return false;
    }
    setReportHandler(elogReportHandler);
    setReportLevel(elogReportLevel);  // env setting can override this
    if (!initGlobals()) {
        return false;
    }
    if (configFile != nullptr && !configureByFile(configFile)) {
        termGlobals();
        return false;
    }
    sMaxThreads = maxThreads;
    sInitialized = true;
    return true;
}

void terminate() {
    if (!sInitialized) {
        ELOG_REPORT_ERROR("Duplicate attempt to terminate ignored");
        return;
    }
    termGlobals();
    sInitialized = false;
}

bool isInitialized() { return sInitialized; }

ELogLogger* getPreInitLogger() { return &sPreInitLogger; }

void discardAccumulatedLogMessages() { sPreInitLogger.discardAccumulatedLogMessages(); }

void setReportLevelFromEnv() {
    std::string elogReportLevel;
    if (elog_getenv("ELOG_REPORT_LEVEL", elogReportLevel)) {
        ELogLevel reportLevel = ELEVEL_WARN;
        if (!elogLevelFromStr(elogReportLevel.c_str(), reportLevel)) {
            fprintf(stderr,
                    "Invalid value for ELOG_REPORT_LEVEL environment variable was ignored: %s\n",
                    elogReportLevel.c_str());
        } else {
            setReportLevel(reportLevel);
        }
    }
}

void setReportHandler(ELogReportHandler* reportHandler) {
    ELogReport::setReportHandler(reportHandler);
}

void setReportLevel(ELogLevel reportLevel) { ELogReport::setReportLevel(reportLevel); }

ELogLevel getReportLevel() { return ELogReport::getReportLevel(); }

bool registerSchemaHandler(const char* schemeName, ELogSchemaHandler* schemaHandler) {
    return ELogSchemaManager::registerSchemaHandler(schemeName, schemaHandler);
}

bool configureRateLimit(const std::string& rateLimitCfg) {
    uint32_t maxMsgPerSec = 0;
    if (!parseIntProp(ELOG_RATE_LIMIT_CONFIG_NAME, "", rateLimitCfg, maxMsgPerSec)) {
        ELOG_REPORT_ERROR("Failed to parse rate limit configuration: ", rateLimitCfg.c_str());
        return false;
    }
    return setRateLimit(maxMsgPerSec);
}

bool configureLogTargetImpl(const std::string& logTargetCfg, ELogTargetId* id /* = nullptr */) {
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

    // load the target (common properties already configured)
    ELogTarget* logTarget = ELogConfigLoader::loadLogTarget(logTargetCfg.c_str());
    if (logTarget == nullptr) {
        return false;
    }

    // finally add the log target
    if (addLogTarget(logTarget) == ELOG_INVALID_TARGET_ID) {
        ELOG_REPORT_ERROR("Failed to add log target %s with scheme %s", logTarget->getName(),
                          logTarget->getTypeName());
        delete logTarget;
        return false;
    }
    if (id != nullptr) {
        *id = logTarget->getId();
    }
    return true;
}

bool configureLogTargetNode(const ELogConfigMapNode* logTargetCfg,
                            ELogTargetId* id /* = nullptr */) {
    // load the target (common properties already configured)
    ELogTarget* logTarget = ELogConfigLoader::loadLogTarget(logTargetCfg);
    if (logTarget == nullptr) {
        return false;
    }

    // finally add the log target
    if (addLogTarget(logTarget) == ELOG_INVALID_TARGET_ID) {
        ELOG_REPORT_ERROR("Failed to add log target %s with scheme %s (context: %s)",
                          logTarget->getName(), logTarget->getTypeName(),
                          logTargetCfg->getFullContext());
        delete logTarget;
        return false;
    }
    if (id != nullptr) {
        *id = logTarget->getId();
    }
    return true;
}

bool configureByPropFile(const char* configPath, bool defineLogSources /* = true */,
                         bool defineMissingPath /* = true */) {
    // elog requires properties in order due to log level propagation
    ELogPropertySequence props;
    if (!ELogConfigLoader::loadFileProperties(configPath, props)) {
        return false;
    }
    return configureByProps(props, defineLogSources, defineMissingPath);
}

bool configureByProps(const ELogPropertySequence& props, bool defineLogSources /* = true */,
                      bool defineMissingPath /* = true */) {
    // TODO: Allow override from env also log_format, log_filter, and perhaps global flush policy

    // configure log format (unrelated to order of appearance)
    // NOTE: only one such item is expected
    std::string logFormatCfg;
    if (getProp(props, ELOG_FORMAT_CONFIG_NAME, logFormatCfg)) {
        if (!configureLogFormat(logFormatCfg.c_str())) {
            ELOG_REPORT_ERROR("Invalid log format in properties: %s", logFormatCfg.c_str());
            return false;
        }
    }

    // configure global filter
    std::string logFilterCfg;
    if (getProp(props, ELOG_FILTER_CONFIG_NAME, logFilterCfg)) {
        if (!configureLogFilter(logFilterCfg.c_str())) {
            return false;
        }
    }

    // configure global rate limit (overrides global filter)
    std::string rateLimitCfg;
    if (getProp(props, ELOG_RATE_LIMIT_CONFIG_NAME, rateLimitCfg)) {
        if (!configureRateLimit(rateLimitCfg)) {
            return false;
        }
    }

    // configure global log level format (font/color)
    /*std::string logLevelFormatCfg;
    if (getProp(props, ELOG_LEVEL_FORMAT_CONFIG_NAME, logLevelFormatCfg)) {
        if (!configureLogLevelFormat(logLevelFormatCfg.c_str())) {
            return false;
        }
    }*/

    std::vector<ELogLevelCfg> logLevelCfg;
    ELogLevel logLevel = ELEVEL_INFO;
    ELogPropagateMode propagateMode = ELogPropagateMode::PM_NONE;

    const char* logLevelSuffix1 = "." ELOG_LEVEL_CONFIG_NAME;  // for configuration files
    const char* logLevelSuffix2 = "_" ELOG_LEVEL_CONFIG_NAME;  // for environment variables
    size_t logLevelSuffixLen = strlen(logLevelSuffix1);

    const char* logAffinitySuffix1 = "." ELOG_AFFINITY_CONFIG_NAME;  // for configuration files
    const char* logAffinitySuffix2 = "_" ELOG_AFFINITY_CONFIG_NAME;  // for environment variables
    size_t logAffinitySuffixLen = strlen(logAffinitySuffix1);

    // get configuration also from env
    ELogPropertySequence envProps;
    char** envPtr = environ;
    for (; *envPtr; envPtr++) {
        std::string envVar(*envPtr);
        std::string::size_type equalPos = envVar.find('=');
        if (equalPos != std::string::npos) {
            std::string envVarName = envVar.substr(0, equalPos);
            std::string envVarValue = envVar.substr(equalPos + 1);
            if (envVarName.ends_with(ELOG_LEVEL_CONFIG_NAME)) {
                ELOG_REPORT_TRACE("Adding prop %s = %s from env", envVarName.c_str(),
                                  envVarValue.c_str());
                envProps.push_back({envVarName, envVarValue});
            }
            if (envVarName.ends_with(ELOG_AFFINITY_CONFIG_NAME)) {
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
        if (prop.first.compare(ELOG_LEVEL_CONFIG_NAME) == 0) {
            // global log level
            if (!ELogConfigParser::parseLogLevel(prop.second.c_str(), logLevel, propagateMode)) {
                ELOG_REPORT_ERROR("Invalid global log level: %s", prop.second.c_str());
                return false;
            }
            logLevelCfg.push_back({getRootLogSource(), logLevel, propagateMode});
            continue;
        }

        // check for log target
        if (prop.first.compare(ELOG_TARGET_CONFIG_NAME) == 0) {
            // configure log target
            if (!configureLogTargetImpl(prop.second)) {
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
            std::string sourceName = key.substr(0, key.size() - logAffinitySuffixLen);
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

bool configureByPropFileEx(const char* configPath, bool defineLogSources /* = true */,
                           bool defineMissingPath /* = true */) {
    // elog requires properties in order due to log level propagation
    ELogConfig* config = ELogConfig::loadFromPropFile(configPath);
    if (config == nullptr) {
        ELOG_REPORT_ERROR("Failed to load configuration from properties file: %s", configPath);
        return false;
    }
    bool res = configure(config, defineLogSources, defineMissingPath);
    delete config;
    return res;
}

bool configureByPropsEx(const ELogPropertyPosSequence& props, bool defineLogSources /* = true */,
                        bool defineMissingPath /* = true */) {
    // we first convert properties to configuration object and then load from cfg object
    ELogConfig* config = ELogConfig::loadFromProps(props);
    if (config == nullptr) {
        ELOG_REPORT_ERROR("Failed to load configuration from properties");
        return false;
    }
    bool res = configure(config, defineLogSources, defineMissingPath);
    delete config;
    return res;
}

bool configureByFile(const char* configPath, bool defineLogSources /* = true */,
                     bool defineMissingPath /* = true */) {
    // elog requires properties in order due to log level propagation
    ELogConfig* config = ELogConfig::loadFromFile(configPath);
    if (config == nullptr) {
        ELOG_REPORT_ERROR("Failed to load configuration from file: %s", configPath);
        return false;
    }
    bool res = configure(config, defineLogSources, defineMissingPath);
    delete config;
    return res;
}

bool augmentConfigFromEnv(ELogConfigMapNode* cfgMap) {
    char** envPtr = environ;
    for (; *envPtr; envPtr++) {
        std::string envVar(*envPtr);
        std::string::size_type equalPos = envVar.find('=');
        if (equalPos != std::string::npos) {
            std::string envVarName = envVar.substr(0, equalPos);
            std::string envVarValue = envVar.substr(equalPos + 1);

            // check for log_level, log_format, log_filter, log_rate_limit
            if (envVarName.compare(ELOG_LEVEL_CONFIG_NAME) == 0 ||
                envVarName.compare(ELOG_FORMAT_CONFIG_NAME) == 0 ||
                envVarName.compare(ELOG_FILTER_CONFIG_NAME) == 0) {
                ELOG_REPORT_TRACE("Overriding %s from env: %s", envVarName.c_str(),
                                  envVarValue.c_str());
                if (!cfgMap->mergeStringEntry(envVarName.c_str(), envVarValue.c_str())) {
                    ELOG_REPORT_ERROR("Failed to merge %s from environment variables (context: %s)",
                                      envVarName.c_str(), cfgMap->getFullContext());
                    return false;
                }
            } else if (envVarName.compare(ELOG_RATE_LIMIT_CONFIG_NAME) == 0) {
                ELOG_REPORT_TRACE("Overriding " ELOG_RATE_LIMIT_CONFIG_NAME " from env: %s",
                                  envVarValue.c_str());
                int64_t rateLimit = 0;
                if (!parseIntProp(ELOG_RATE_LIMIT_CONFIG_NAME, "N/A", envVarValue, rateLimit)) {
                    ELOG_REPORT_ERROR("Invalid " ELOG_RATE_LIMIT_CONFIG_NAME
                                      " environment variable value %s, expecting integer "
                                      "(context: %s)",
                                      envVarValue.c_str(), cfgMap->getFullContext());
                    return false;
                }
                if (!cfgMap->mergeIntEntry(envVarName.c_str(), rateLimit)) {
                    ELOG_REPORT_ERROR("Failed to merge " ELOG_RATE_LIMIT_CONFIG_NAME
                                      " from environment variables (context: %s)",
                                      cfgMap->getFullContext());
                    return false;
                }
            }

            // check for variables that end with _log_level or _log_affinity
            else if (envVarName.ends_with(ELOG_LEVEL_CONFIG_NAME) ||
                     envVarName.ends_with(ELOG_AFFINITY_CONFIG_NAME)) {
                ELOG_REPORT_TRACE("Overriding %s = %s from env", envVarName.c_str(),
                                  envVarValue.c_str());
                if (!cfgMap->mergeStringEntry(envVarName.c_str(), envVarValue.c_str())) {
                    ELOG_REPORT_ERROR("Failed to merge %s from environment variables (context: %s)",
                                      envVarName.c_str(), cfgMap->getFullContext());
                    return false;
                }
            }
        }
    }
    return true;
}

bool configureByStr(const char* configStr, bool defineLogSources /* = true */,
                    bool defineMissingPath /* = true */) {
    ELogConfig* config = ELogConfig::loadFromString(configStr);
    if (config == nullptr) {
        ELOG_REPORT_ERROR("Failed to load configuration from string: %s", configStr);
        return false;
    }
    bool res = configure(config, defineLogSources, defineMissingPath);
    delete config;
    return res;
}

inline bool validateConfigValueType(const ELogConfigValue* value, ELogConfigValueType type,
                                    const char* key) {
    if (value->getValueType() != type) {
        ELOG_REPORT_ERROR(
            "Invalid configuration value type for %s, expecting %s, seeing instead %s (context: "
            "%s)",
            key, configValueTypeToString(type), configValueTypeToString(value->getValueType()),
            value->getFullContext());
        return false;
    }
    return true;
}

inline bool validateConfigValueStringType(const ELogConfigValue* value, const char* key) {
    return validateConfigValueType(value, ELogConfigValueType::ELOG_CONFIG_STRING_VALUE, key);
}

inline bool validateConfigValueIntType(const ELogConfigValue* value, const char* key) {
    return validateConfigValueType(value, ELogConfigValueType::ELOG_CONFIG_INT_VALUE, key);
}

bool configure(ELogConfig* config, bool defineLogSources /* = true */,
               bool defineMissingPath /* = true */) {
    // verify root node is of map type
    if (config->getRootNode()->getNodeType() != ELogConfigNodeType::ELOG_CONFIG_MAP_NODE) {
        ELOG_REPORT_ERROR("Top-level configuration node is not a map node");
        return false;
    }
    ELogConfigMapNode* cfgMap = (ELogConfigMapNode*)config->getRootNode();

    // augment with environment variables
    if (!augmentConfigFromEnv(cfgMap)) {
        ELOG_REPORT_ERROR("Failed to augment configuration object from environment variables");
        return false;
    }

    // now configure

    // configure global log format
    bool found = false;
    std::string logFormatCfg;
    if (!cfgMap->getStringValue(ELOG_FORMAT_CONFIG_NAME, found, logFormatCfg)) {
        // configuration error
        return false;
    } else if (found && !configureLogFormat(logFormatCfg.c_str())) {
        ELOG_REPORT_ERROR("Invalid top-level log format in properties: %s", logFormatCfg.c_str());
        return false;
    }

    // configure global filter
    std::string logFilterCfg;
    if (!cfgMap->getStringValue(ELOG_FILTER_CONFIG_NAME, found, logFilterCfg)) {
        // configuration error
        return false;
    } else if (found && !configureLogFilter(logFilterCfg.c_str())) {
        ELOG_REPORT_ERROR("Invalid top-level log filter in properties: %s", logFilterCfg.c_str());
        return false;
    }

    // configure global rate limit (overrides global filter)
    // TODO: what about valid values? should be defined and checked
    int64_t rateLimit = 0;
    if (!cfgMap->getIntValue(ELOG_RATE_LIMIT_CONFIG_NAME, found, rateLimit)) {
        // configuration error
        return false;
    } else if (found && !setRateLimit((uint32_t)rateLimit)) {
        return false;
    }

    std::vector<ELogLevelCfg> logLevelCfg;
    ELogLevel logLevel = ELEVEL_INFO;
    ELogPropagateMode propagateMode = ELogPropagateMode::PM_NONE;

    const char* logLevelSuffix1 = "." ELOG_LEVEL_CONFIG_NAME;  // for configuration files
    const char* logLevelSuffix2 = "_" ELOG_LEVEL_CONFIG_NAME;  // for environment variables
    size_t logLevelSuffixLen = strlen(logLevelSuffix1);

    const char* logAffinitySuffix1 = "." ELOG_AFFINITY_CONFIG_NAME;  // for configuration files
    const char* logAffinitySuffix2 = "_" ELOG_AFFINITY_CONFIG_NAME;  // for environment variables
    size_t logAffinitySuffixLen = strlen(logAffinitySuffix1);

    for (size_t i = 0; i < cfgMap->getEntryCount(); ++i) {
        const ELogConfigMapNode::EntryType& prop = cfgMap->getEntryAt(i);
        const ELogConfigValue* cfgValue = prop.second;
        // check if this is root log level
        if (prop.first.compare(ELOG_LEVEL_CONFIG_NAME) == 0) {
            // global log level, should be a string
            if (!validateConfigValueStringType(cfgValue, ELOG_LEVEL_CONFIG_NAME)) {
                return false;
            }
            const char* logLevelStr = ((ELogConfigStringValue*)cfgValue)->getStringValue();
            if (!ELogConfigParser::parseLogLevel(logLevelStr, logLevel, propagateMode)) {
                ELOG_REPORT_ERROR("Invalid global log level: %s", logLevelStr);
                return false;
            }
            // configure later when we have all information gathered
            logLevelCfg.push_back({getRootLogSource(), logLevel, propagateMode});
            continue;
        }

        // check for log target
        if (prop.first.compare(ELOG_TARGET_CONFIG_NAME) == 0) {
            // configure log target
            if (cfgValue->getValueType() == ELogConfigValueType::ELOG_CONFIG_STRING_VALUE) {
                const char* logTargetStr = ((ELogConfigStringValue*)cfgValue)->getStringValue();
                if (!configureLogTargetImpl(logTargetStr)) {
                    ELOG_REPORT_ERROR("Failed to configure log target (context: %s)",
                                      cfgValue->getFullContext());
                    return false;
                }
            } else if (cfgValue->getValueType() == ELogConfigValueType::ELOG_CONFIG_MAP_VALUE) {
                const ELogConfigMapNode* logTargetCfg =
                    ((const ELogConfigMapValue*)cfgValue)->getMapNode();
                if (!configureLogTargetNode(logTargetCfg)) {
                    return false;
                }
            } else {
                ELOG_REPORT_ERROR("Invalid configuration value type for " ELOG_TARGET_CONFIG_NAME
                                  ", expecting either string or "
                                  "map, seeing instead %s (context: %s)",
                                  configValueTypeToString(cfgValue->getValueType()),
                                  cfgValue->getFullContext());
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
            // get log level, should be a string
            if (!validateConfigValueStringType(cfgValue, key.c_str())) {
                return false;
            }
            const char* logLevelStr = ((ELogConfigStringValue*)cfgValue)->getStringValue();
            if (!ELogConfigParser::parseLogLevel(logLevelStr, logLevel, propagateMode)) {
                ELOG_REPORT_ERROR("Invalid source %s log level: %s", sourceName.c_str(),
                                  logLevelStr);
                return false;
            }
            logLevelCfg.push_back({logSource, logLevel, propagateMode});
        }

        // configure log affinity of log sources
        if (prop.first.ends_with(logAffinitySuffix1) || prop.first.ends_with(logAffinitySuffix2)) {
            const std::string& key = prop.first;
            // shave off trailing ".log_level/_log_level" (that includes dot/underscore)
            std::string sourceName = key.substr(0, key.size() - logAffinitySuffixLen);
            ELogSource* logSource = defineLogSources
                                        ? defineLogSource(sourceName.c_str(), defineMissingPath)
                                        : getLogSource(sourceName.c_str());
            if (logSource == nullptr) {
                ELOG_REPORT_ERROR("Invalid log source name: %s", sourceName.c_str());
                return false;
            }
            ELogTargetAffinityMask mask = 0;
            // get log level, should be a string
            if (!validateConfigValueStringType(cfgValue, key.c_str())) {
                return false;
            }
            const char* logAffinityStr = ((ELogConfigStringValue*)cfgValue)->getStringValue();
            if (!ELogConfigParser::parseLogAffinityList(logAffinityStr, mask)) {
                ELOG_REPORT_ERROR("Invalid source %s log affinity specification: %s",
                                  sourceName.c_str(), logAffinityStr);
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

ELogTargetId addLogTarget(ELogTarget* logTarget) {
    // TODO: should we guard against duplicate names (they are used in search by name and in log
    // affinity mask building) - this might require API change (returning at least bool)
    ELOG_REPORT_TRACE("Adding log target: %s", logTarget->getName());

    // we must start the log target early because of statistics dependency (if started after adding
    // to array, then any ELOG_REPORT_XXX in between would trigger logging for the new target before
    // the statistics object was created)
    // TODO: this is too sensitive, it is better to check whether the statistics object exists and
    // get rid of m_enableStats altogether
    if (!logTarget->start()) {
        ELOG_REPORT_ERROR("Failed to start log target %s", logTarget->getName());
        logTarget->setId(ELOG_INVALID_TARGET_ID);
        return ELOG_INVALID_TARGET_ID;
    }

    // find vacant slot ro add a new one
    ELogTargetId logTargetId = ELOG_INVALID_TARGET_ID;
    for (uint32_t i = 0; i < sLogTargets.size(); ++i) {
        if (sLogTargets[i] == nullptr) {
            sLogTargets[i] = logTarget;
            ELOG_REPORT_TRACE("Added log target %s with id %u", logTarget->getName(), i);
            logTargetId = i;
            break;
        }
    }

    // otherwise add a new slot
    if (logTargetId == ELOG_INVALID_TARGET_ID) {
        if (sLogTargets.size() == ELOG_MAX_TARGET_COUNT) {
            ELOG_REPORT_ERROR("Cannot add log target, reached hard limit of log targets %u",
                              ELOG_MAX_TARGET_COUNT);
            logTarget->stop();
            return ELOG_INVALID_TARGET_ID;
        }
        logTargetId = (ELogTargetId)sLogTargets.size();
        sLogTargets.push_back(logTarget);
        ELOG_REPORT_TRACE("Added log target %s with id %u", logTarget->getName(), logTargetId);
    }

    // set target id and start it
    logTarget->setId(logTargetId);

    // write accumulated log messages if there are such
    sPreInitLogger.writeAccumulatedLogMessages(logTarget);
    return logTargetId;
}

ELogTargetId configureLogTarget(const char* logTargetCfg) {
    ELogTargetId id = ELOG_INVALID_TARGET_ID;
    if (!configureLogTargetImpl(logTargetCfg, &id)) {
        return ELOG_INVALID_TARGET_ID;
    }
    return id;
}

ELogTargetId addLogFileTarget(const char* logFilePath, uint32_t bufferSize /* = 0 */,
                              bool useLock /* = false */, uint32_t segmentLimitMB /* = 0 */,
                              uint32_t segmentCount /* = 0 */, bool enableStats /* = true */,
                              ELogLevel logLevel /* = ELEVEL_INFO */,
                              ELogFlushPolicy* flushPolicy /* = nullptr */,
                              ELogFilter* logFilter /* = nullptr */,
                              ELogFormatter* logFormatter /* = nullptr */) {
    // we delegate to the schema handler
    ELogTarget* logTarget = ELogFileSchemaHandler::createLogTarget(
        logFilePath, bufferSize, useLock, segmentLimitMB, 0, segmentCount, enableStats);
    if (logTarget == nullptr) {
        return ELOG_INVALID_TARGET_ID;
    }

    logTarget->setLogLevel(logLevel);
    if (flushPolicy != nullptr) {
        logTarget->setFlushPolicy(flushPolicy);
    }
    if (logFilter != nullptr) {
        logTarget->setLogFilter(logFilter);
    }
    if (logFormatter != nullptr) {
        logTarget->setLogFormatter(logFormatter);
    }

    ELogTargetId logTargetId = addLogTarget(logTarget);
    if (logTargetId == ELOG_INVALID_TARGET_ID) {
        // NOTE: detach from policy/filter/formatter before delete, because in case of failure
        // caller is still owner of these objects.
        logTarget->detach();
        delete logTarget;
    }

    return logTargetId;
}

ELogTargetId attachLogFileTarget(FILE* fileHandle, bool closeHandleWhenDone /* = false */,
                                 uint32_t bufferSize /* = 0 */, bool useLock /* = false */,
                                 bool enableStats /* = true */,
                                 ELogLevel logLevel /* = ELEVEL_INFO */,
                                 ELogFlushPolicy* flushPolicy /* = nullptr */,
                                 ELogFilter* logFilter /* = nullptr */,
                                 ELogFormatter* logFormatter /* = nullptr */) {
    ELogTarget* logTarget = nullptr;
    if (bufferSize > 0) {
        logTarget = new (std::nothrow) ELogBufferedFileTarget(
            fileHandle, bufferSize, useLock, flushPolicy, closeHandleWhenDone, enableStats);
    } else {
        logTarget = new (std::nothrow)
            ELogFileTarget(fileHandle, flushPolicy, closeHandleWhenDone, enableStats);
    }
    if (logTarget == nullptr) {
        ELOG_REPORT_ERROR("Failed to create log target, out of memory");
        return ELOG_INVALID_TARGET_ID;
    }

    logTarget->setLogLevel(logLevel);
    if (flushPolicy != nullptr) {
        logTarget->setFlushPolicy(flushPolicy);
    }
    if (logFilter != nullptr) {
        logTarget->setLogFilter(logFilter);
    }
    if (logFormatter != nullptr) {
        logTarget->setLogFormatter(logFormatter);
    }

    ELogTargetId logTargetId = addLogTarget(logTarget);
    if (logTargetId == ELOG_INVALID_TARGET_ID) {
        // NOTE: detach from policy/filter/formatter before delete, because in case of failure
        // caller is still owner of these objects.
        logTarget->detach();
        delete logTarget;
    }

    return logTargetId;
}

ELogTargetId addStdErrLogTarget(ELogLevel logLevel /* = ELEVEL_INFO */,
                                ELogFilter* logFilter /* = nullptr */,
                                ELogFormatter* logFormatter /* = nullptr */) {
    return attachLogFileTarget(stderr, false, 0, false, false, logLevel, nullptr, logFilter,
                               logFormatter);
}

ELogTargetId addStdOutLogTarget(ELogLevel logLevel /* = ELEVEL_INFO */,
                                ELogFilter* logFilter /* = nullptr */,
                                ELogFormatter* logFormatter /* = nullptr */) {
    return attachLogFileTarget(stdout, false, 0, false, false, logLevel, nullptr, logFilter,
                               logFormatter);
}

ELogTargetId addSysLogTarget(ELogLevel logLevel /* = ELEVEL_INFO */,
                             ELogFilter* logFilter /* = nullptr */,
                             ELogFormatter* logFormatter /* = nullptr */) {
#ifdef ELOG_LINUX
    ELogSysLogTarget* logTarget = new (std::nothrow) ELogSysLogTarget();
    if (logTarget == nullptr) {
        ELOG_REPORT_ERROR("Failed to create syslog target, out of memory");
        return ELOG_INVALID_TARGET_ID;
    }

    logTarget->setLogLevel(logLevel);
    if (logFilter != nullptr) {
        logTarget->setLogFilter(logFilter);
    }
    if (logFormatter != nullptr) {
        logTarget->setLogFormatter(logFormatter);
    }

    ELogTargetId logTargetId = addLogTarget(logTarget);
    if (logTargetId == ELOG_INVALID_TARGET_ID) {
        // NOTE: detach from policy/filter/formatter before delete, because in case of failure
        // caller is still owner of these objects.
        logTarget->detach();
        delete logTarget;
    }

    return logTargetId;
#else
    ELOG_REPORT_ERROR("Cannot create syslog target: not supported on current platform");
    return ELOG_INVALID_TARGET_ID;
#endif
}

ELogTargetId addWin32EventLogTarget(ELogLevel logLevel /* = ELEVEL_INFO */,
                                    const char* eventSourceName /* = "" */,
                                    uint32_t eventId /* = 0 */,
                                    ELogFilter* logFilter /* = nullptr */,
                                    ELogFormatter* logFormatter /* = nullptr */) {
#ifdef ELOG_WINDOWS
    ELogWin32EventLogTarget* logTarget =
        new (std::nothrow) ELogWin32EventLogTarget(eventSourceName, eventId);
    if (logTarget == nullptr) {
        ELOG_REPORT_ERROR("Failed to create Windows Event Log target, out of memory");
        return ELOG_INVALID_TARGET_ID;
    }

    logTarget->setLogLevel(logLevel);
    if (logFilter != nullptr) {
        logTarget->setLogFilter(logFilter);
    }
    if (logFormatter != nullptr) {
        logTarget->setLogFormatter(logFormatter);
    }

    ELogTargetId logTargetId = addLogTarget(logTarget);
    if (logTargetId == ELOG_INVALID_TARGET_ID) {
        // NOTE: detach from policy/filter/formatter before delete, because in case of failure
        // caller is still owner of these objects.
        logTarget->detach();
        delete logTarget;
    }

    return logTargetId;
#else
    ELOG_REPORT_ERROR("Cannot create Windows Event Log target: not supported on current platform");
    return ELOG_INVALID_TARGET_ID;
#endif
}

ELogTargetId addTracer(const char* traceFilePath, uint32_t traceBufferSize, const char* targetName,
                       const char* sourceName) {
    // prepare configuration string
    std::stringstream s;
    s << "async://quantum?quantum_buffer_size=" << traceBufferSize << "&name=" << targetName
      << " | file:///" << traceFilePath << "?flush_policy=immediate";
    std::string cfg = s.str();

    // add log target from configuration string
    ELogTargetId id = configureLogTarget(cfg.c_str());
    if (id == ELOG_INVALID_TARGET_ID) {
        return id;
    }
    ELogTarget* logTarget = getLogTarget(id);
    if (logTarget == nullptr) {
        ELOG_REPORT_ERROR("Internal error while adding tracer, log target by id %u not found", id);
        return false;
    }

    // define a pass key to the trace target, so that normal log messages will not reach the tracer
    logTarget->setPassKey();

    // define log source
    ELogSource* logSource = defineLogSource(sourceName, true);
    if (logSource == nullptr) {
        ELOG_REPORT_ERROR("Failed to define tracer %s log source by name %s", targetName,
                          sourceName);
        return false;
    }

    // bind log source to target using affinity mask
    ELogTargetAffinityMask mask;
    ELOG_CLEAR_TARGET_AFFINITY_MASK(mask);
    ELOG_ADD_TARGET_AFFINITY_MASK(mask, logTarget->getId());
    logSource->setLogTargetAffinity(mask);

    // add pass key to the log source
    logSource->addPassKey(logTarget->getPassKey());
    return id;
}

ELogTarget* getLogTarget(ELogTargetId targetId) {
    if (targetId >= sLogTargets.size()) {
        return nullptr;
    }
    return sLogTargets[targetId];
}

ELogTarget* getLogTarget(const char* logTargetName) {
    for (ELogTarget* logTarget : sLogTargets) {
        if (strcmp(logTarget->getName(), logTargetName) == 0) {
            return logTarget;
        }
    }
    return nullptr;
}

ELogTargetId getLogTargetId(const char* logTargetName) {
    for (uint32_t logTargetId = 0; logTargetId < sLogTargets.size(); ++logTargetId) {
        ELogTarget* logTarget = sLogTargets[logTargetId];
        if (strcmp(logTarget->getName(), logTargetName) == 0) {
            return logTargetId;
        }
    }
    return ELOG_INVALID_TARGET_ID;
}

static void compactLogTargets() {
    // find largest suffix of removed log targets
    size_t maxLogTargetId = sLogTargets.size() - 1;
    bool done = false;
    while (!done) {
        if (sLogTargets[maxLogTargetId] != nullptr) {
            sLogTargets.resize(maxLogTargetId + 1);
            ELOG_REPORT_TRACE("Log target array compacted to %zu entries", sLogTargets.size());
            done = true;
        } else if (maxLogTargetId == 0) {
            // last one is also null
            sLogTargets.clear();
            ELOG_REPORT_TRACE("Log target array fully truncated");
            done = true;
        } else {
            --maxLogTargetId;
        }
    }
}

void removeLogTarget(ELogTargetId targetId) {
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

    // if suffix entries contain nulls we can reduce array size
    compactLogTargets();
}

void clearAllLogTargets() {
    // NOTE: since log target may have indirect dependencies (e.g. one log target, while writing a
    // log message, issues another log message which is dispatched to all other log targets), we
    // first stop all targets and then delete them, but this requires log target to be able to
    // reject log messages after stop() was called.
    for (ELogTarget* logTarget : sLogTargets) {
        if (sIsTerminating || (logTarget != nullptr && !logTarget->isSystemTarget())) {
            logTarget->stop();
        }
    }
    for (uint32_t i = 0; i < sLogTargets.size(); ++i) {
        ELogTarget* logTarget = sLogTargets[i];
        if (sIsTerminating || (logTarget != nullptr && !logTarget->isSystemTarget())) {
            delete logTarget;
            sLogTargets[i] = nullptr;
        }
    }
    compactLogTargets();
}

void removeLogTarget(ELogTarget* target) {
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

ELogSource* createLogSource(ELogSourceId sourceId, const char* name,
                            ELogSource* parent /* = nullptr */,
                            ELogLevel logLevel /* = ELEVEL_INFO */) {
    return new (std::nothrow) ELogSource(sourceId, name, parent, logLevel);
}

// control carefully who can delete a log source
void deleteLogSource(ELogSource* logSource) { delete logSource; }

ELogSource* addChildSource(ELogSource* parent, const char* sourceName) {
    ELogSource* logSource = createLogSource(allocLogSourceId(), sourceName, parent);
    if (!parent->addChild(logSource)) {
        // impossible
        ELOG_REPORT_ERROR("Internal error, cannot add child source %s, already exists", sourceName);
        deleteLogSource(logSource);
        return nullptr;
    }

    bool res =
        sLogSourceMap.insert(ELogSourceMap::value_type(logSource->getId(), logSource)).second;
    if (!res) {
        // internal error, roll back
        ELOG_REPORT_ERROR("Internal error, cannot add new log source %s by id %u, already exists",
                          sourceName, logSource->getId());
        parent->removeChild(logSource->getName());
        deleteLogSource(logSource);
        return nullptr;
    }
    return logSource;
}

// log sources
ELogSource* defineLogSource(const char* qualifiedName, bool defineMissingPath /* = true */) {
    if (qualifiedName == nullptr || *qualifiedName == 0) {
        return sRootLogSource;
    }
    std::unique_lock<std::mutex> lock(sSourceTreeLock);
    // parse name to components and start traveling up to last component
    std::vector<std::string> namePath;
    parseSourceName(qualifiedName, namePath);

    ELogSource* currSource = sRootLogSource;
    size_t nameCount = namePath.size();
    for (size_t i = 0; i < nameCount - 1; ++i) {
        ELogSource* childSource = currSource->getChild(namePath[i].c_str());
        if (childSource == nullptr && defineMissingPath) {
            childSource = addChildSource(currSource, namePath[i].c_str());
        }
        if (childSource == nullptr) {
            // TODO: partial failures are left as dangling sources, is that ok?
            if (defineMissingPath) {
                ELOG_REPORT_ERROR("Failed to define log source %s: failed to define path part %s",
                                  qualifiedName, namePath[i].c_str());
            } else {
                ELOG_REPORT_ERROR("Cannot define log source %s: missing path part %s",
                                  qualifiedName, namePath[i].c_str());
            }
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
    if (logSource == nullptr) {
        ELOG_REPORT_ERROR("Failed to define log source %s: failed to add child %s to parent %s",
                          logSourceName, currSource->getQualifiedName());
        return nullptr;
    }

    // in case of a new log source, we check if there is an environment variable for configuring
    // its log level. The expected format is: <qualified-log-source-name>_log_level =
    // <elog-level> every dot in the qualified name is replaced with underscore
    std::string envVarName = std::string(qualifiedName) + "_log_level";
    std::replace(envVarName.begin(), envVarName.end(), '.', '_');
    std::string envVarValue;
    if (elog_getenv(envVarName.c_str(), envVarValue)) {
        ELogLevel logLevel = ELEVEL_INFO;
        if (elogLevelFromStr(envVarValue.c_str(), logLevel)) {
            logSource->setLogLevel(logLevel, ELogPropagateMode::PM_NONE);
        }
    }

    return logSource;
}

ELogSource* getLogSource(const char* qualifiedName) {
    std::unique_lock<std::mutex> lock(sSourceTreeLock);
    std::vector<std::string> namePath;
    parseSourceName(qualifiedName, namePath);
    ELogSource* currSource = sRootLogSource;
    size_t nameCount = namePath.size();
    for (size_t i = 0; i < nameCount; ++i) {
        currSource = currSource->getChild(namePath[i].c_str());
        if (currSource == nullptr) {
            ELOG_REPORT_ERROR("Cannot retrieve log source %s: missing path part %s", qualifiedName,
                              namePath[i].c_str());
            break;
        }
    }
    return currSource;
}

ELogSource* getLogSource(ELogSourceId logSourceId) {
    ELogSource* logSource = nullptr;
    std::unique_lock<std::mutex> lock(sSourceTreeLock);
    ELogSourceMap::iterator itr = sLogSourceMap.find(logSourceId);
    if (itr != sLogSourceMap.end()) {
        logSource = itr->second;
    }
    return logSource;
}

ELogSource* getRootLogSource() { return sRootLogSource; }

// void configureLogSourceLevel(const char* qualifiedName, ELogLevel logLevel);

// logger interface
ELogLogger* getDefaultLogger() { return sDefaultLogger; }

ELogLogger* getSharedLogger(const char* qualifiedSourceName,
                            bool defineLogSourceIfMissing /* = true */,
                            bool defineMissingPath /* = true */) {
    ELogLogger* logger = nullptr;
    ELogSource* source = nullptr;
    // if we call getLogSource an error will be printed, but this is incorrect when
    // defineLogSourceIfMissing is set to true
    // in that case we simply define the log source, if it already exists, nothing happens and it
    // will be retrieved
    if (defineLogSourceIfMissing) {
        source = defineLogSource(qualifiedSourceName, defineMissingPath);
    } else {
        source = getLogSource(qualifiedSourceName);
    }
    if (source != nullptr) {
        logger = source->createSharedLogger();
    }
    return logger;
}

ELogLogger* getPrivateLogger(const char* qualifiedSourceName,
                             bool defineLogSourceIfMissing /* = true */,
                             bool defineMissingPath /* = true */) {
    ELogLogger* logger = nullptr;
    ELogSource* source = nullptr;
    // if we call getLogSource an error will be printed, but this is incorrect when
    // defineLogSourceIfMissing is set to true
    // in that case we simply define the log source, if it already exists, nothing happens and it
    // will be retrieved
    if (defineLogSourceIfMissing) {
        source = defineLogSource(qualifiedSourceName, defineMissingPath);
    } else {
        source = getLogSource(qualifiedSourceName);
    }
    if (source != nullptr) {
        logger = source->createPrivateLogger();
    }
    return logger;
}

ELogLevel getLogLevel() { return sRootLogSource->getLogLevel(); }

void setLogLevel(ELogLevel logLevel, ELogPropagateMode propagateMode) {
    return sRootLogSource->setLogLevel(logLevel, propagateMode);
}

/*void setLogLevelFormat(ELogLevel logLevel, const ELogTextSpec& formatSpec) {}

bool configureLogLevelFormat(const char* logLevelConfig) {
    // parse as properties
    ELogPropsFormatter formatter;
    if (!formatter.parseProps(logLevelConfig)) {
        ELOG_REPORT_ERROR("Invalid log level format string: %s", logLevelConfig);
        return false;
    }
}*/

// log formatting
bool configureLogFormat(const char* logFormat) {
    ELogFormatter* logFormatter = new (std::nothrow) ELogFormatter();
    if (!logFormatter->initialize(logFormat)) {
        delete logFormatter;
        return false;
    }
    setLogFormatter(logFormatter);
    return true;
}

void setLogFormatter(ELogFormatter* logFormatter) {
    if (sGlobalFormatter != nullptr) {
        delete sGlobalFormatter;
    }
    sGlobalFormatter = logFormatter;
}

uint32_t getMaxThreads() { return sMaxThreads; }

void resetThreadStatCounters(uint64_t slotId) {
    for (ELogTargetId logTargetId = 0; logTargetId < sLogTargets.size(); ++logTargetId) {
        ELogTarget* logTarget = sLogTargets[logTargetId];
        logTarget->getStats()->resetThreadCounters(slotId);
    }
}

void formatLogMsg(const ELogRecord& logRecord, std::string& logMsg) {
    sGlobalFormatter->formatLogMsg(logRecord, logMsg);
}

void formatLogBuffer(const ELogRecord& logRecord, ELogBuffer& logBuffer) {
    sGlobalFormatter->formatLogBuffer(logRecord, logBuffer);
}

ELogCacheEntryId cacheFormatMsg(const char* fmt) { return ELogCache::cacheFormatMsg(fmt); }

const char* getCachedFormatMsg(ELogCacheEntryId entryId) {
    return ELogCache::getCachedFormatMsg(entryId);
}

ELogCacheEntryId getOrCacheFormatMsg(const char* fmt) {
    return ELogCache::getOrCacheFormatMsg(fmt);
}

void setAppName(const char* appName) { setAppNameField(appName); }

void setCurrentThreadName(const char* threadName) { setCurrentThreadNameField(threadName); }

bool configureLogFilter(const char* logFilterCfg) {
    if (logFilterCfg[0] != '(') {
        ELOG_REPORT_ERROR(
            "Cannot configure global log filter, only expression style is supported: %s",
            logFilterCfg);
        return false;
    }

    ELogFilter* logFilter = ELogConfigLoader::loadLogFilterExprStr(logFilterCfg);
    if (logFilter == nullptr) {
        ELOG_REPORT_ERROR("Failed to configure global log filter from string: %s", logFilterCfg);
        return false;
    }
    setLogFilter(logFilter);
    return true;
}

// global log filtering
void setLogFilter(ELogFilter* logFilter) {
    if (sGlobalFilter != nullptr) {
        delete sGlobalFilter;
    }
    sGlobalFilter = logFilter;
}

bool setRateLimit(uint32_t maxMsgPerSecond, bool replaceGlobalFilter /* = true */) {
    ELogRateLimiter* rateLimiter = new (std::nothrow) ELogRateLimiter(maxMsgPerSecond);
    if (rateLimiter == nullptr) {
        ELOG_REPORT_ERROR("Failed to set rate limit, out of memory");
        return false;
    }
    if (sGlobalFilter == nullptr || replaceGlobalFilter) {
        setLogFilter(rateLimiter);
    }

    // if a global log filter already exists, then the rate limiter ORed with the existing filter
    ELogOrLogFilter* logFilter = new (std::nothrow) ELogOrLogFilter();
    if (logFilter == nullptr) {
        ELOG_REPORT_ERROR(
            "Failed to allocate OR log filter for global rate limiter, out of memory");
        delete rateLimiter;
        return false;
    }
    logFilter->addFilter(sGlobalFilter);
    logFilter->addFilter(rateLimiter);
    setLogFilter(logFilter);
    return true;
}

bool filterLogMsg(const ELogRecord& logRecord) {
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
    LogStackEntryPrinter(ELogLogger* logger, ELogLevel logLevel, const char* title)
        : m_logger(logger), m_logLevel(logLevel), m_title(title) {}
    LogStackEntryPrinter(const LogStackEntryPrinter&) = delete;
    LogStackEntryPrinter(LogStackEntryPrinter&&) = delete;
    LogStackEntryPrinter& operator=(const LogStackEntryPrinter&) = delete;
    ~LogStackEntryPrinter() override {}

    void onBeginStackTrace(dbgutil::os_thread_id_t threadId) override {
        // std::string threadName;
        // getThreadName(threadId, threadName);
        std::string threadName = getThreadNameField(threadId);
        if (m_title.empty()) {
            if (threadName.empty()) {
                ELOG_BEGIN_EX(m_logger, m_logLevel,
                              "[Thread %" PRItid " (0x%" PRItidx ") stack trace]\n", threadId,
                              threadId);
            } else {
                ELOG_BEGIN_EX(m_logger, m_logLevel,
                              "[Thread %" PRItid " (0x%" PRItidx ") <%s> stack trace]\n", threadId,
                              threadId, threadName.c_str());
            }
        } else {
            if (threadName.empty()) {
                ELOG_BEGIN_EX(m_logger, m_logLevel,
                              "%s:\n[Thread %" PRItid " (0x%" PRItidx ") stack trace]\n",
                              m_title.c_str(), threadId, threadId);
            } else {
                ELOG_BEGIN_EX(m_logger, m_logLevel,
                              "%s:\n[Thread %" PRItid " (0x%" PRItidx ") <%s> stack trace]\n",
                              m_title.c_str(), threadId, threadId, threadName.c_str());
            }
        }
    }
    void onEndStackTrace() override { ELOG_END_EX(m_logger); }
    void onStackEntry(const char* stackEntry) {
        ELOG_APPEND_EX(m_logger, m_logLevel, "%s\n", stackEntry);
    }

private:
    ELogLogger* m_logger;
    ELogLevel m_logLevel;
    std::string m_title;
};

void logStackTrace(ELogLogger* logger, ELogLevel logLevel, const char* title, int skip,
                   dbgutil::StackEntryFormatter* formatter) {
    LogStackEntryPrinter printer(logger, logLevel, title == nullptr ? "" : title);
    dbgutil::printStackTrace(skip, &printer, formatter);
}

void logStackTraceContext(ELogLogger* logger, void* context, ELogLevel logLevel, const char* title,
                          int skip, dbgutil::StackEntryFormatter* formatter) {
    LogStackEntryPrinter printer(logger, logLevel, title == nullptr ? "" : title);
    dbgutil::printStackTraceContext(context, skip, &printer, formatter);
}

void logAppStackTrace(ELogLogger* logger, ELogLevel logLevel, const char* title, int skip,
                      dbgutil::StackEntryFormatter* formatter) {
    LogStackEntryPrinter printer(logger, logLevel, title == nullptr ? "" : title);
    dbgutil::printAppStackTrace(skip, &printer, formatter);
}
#endif

void logMsg(const ELogRecord& logRecord,
            ELogTargetAffinityMask logTargetAffinityMask /* = ELOG_ALL_TARGET_AFFINITY_MASK */) {
    bool logged = false;
    for (ELogTargetId logTargetId = 0; logTargetId < sLogTargets.size(); ++logTargetId) {
        ELogTarget* logTarget = sLogTargets[logTargetId];
        if (logTargetId > ELOG_MAX_LOG_TARGET_ID_AFFINITY ||
            ELOG_HAS_TARGET_AFFINITY_MASK(logTargetAffinityMask, logTargetId)) {
            // check also pass key if present
            ELogPassKey passKey = logTarget->getPassKey();
            if (passKey == ELOG_NO_PASSKEY ||
                logRecord.m_logger->getLogSource()->hasPassKey(passKey)) {
                logTarget->log(logRecord);
                logged = true;
            }
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

char* sysErrorToStr(int sysErrorCode) { return ELogReport::sysErrorToStr(sysErrorCode); }

#ifdef ELOG_WINDOWS
char* win32SysErrorToStr(unsigned long sysErrorCode) {
    return ELogReport::win32SysErrorToStr(sysErrorCode);
}

void win32FreeErrorStr(char* errStr) { ELogReport::win32FreeErrorStr(errStr); }
#endif

ELogRecord ELogModerate::m_dummy;

bool ELogModerate::moderate() {
    if (m_rateLimiter.filterLogRecord(m_dummy)) {
        // first to pass since last time started discarding, should print aggregation stats
        bool isDiscarding = m_isDiscarding.load(std::memory_order_acquire);
        if (isDiscarding) {
            if (m_isDiscarding.compare_exchange_strong(isDiscarding, false,
                                                       std::memory_order_release)) {
                uint64_t endDiscardCount = m_discardCount.load(std::memory_order_relaxed);
                uint64_t discardCount = endDiscardCount - m_startDiscardCount;
                std::chrono::steady_clock::time_point endDiscardTime =
                    std::chrono::steady_clock::now();
                std::chrono::milliseconds discardTimeMillis =
                    std::chrono::duration_cast<std::chrono::milliseconds>(endDiscardTime -
                                                                          m_startDiscardTime);
                ELOG_REPORT_INFO("The message '%s' has been discarded for %" PRIu64
                                 " times in the last %" PRId64 " milliseconds",
                                 m_fmt, discardCount, discardTimeMillis.count());
            }
        }
        return true;
    }

    // raise is-discarding flag if needed
    bool isDiscarding = m_isDiscarding.load(std::memory_order_acquire);
    if (!isDiscarding) {
        // let the first one that makes the switch to save discard period counter values
        if (m_isDiscarding.compare_exchange_strong(isDiscarding, true, std::memory_order_release)) {
            m_startDiscardCount = m_discardCount.load(std::memory_order_relaxed);
            m_startDiscardTime = std::chrono::steady_clock::now();
        }
    }

    // increment discard count and return
    m_discardCount.fetch_add(1, std::memory_order_relaxed);
    return false;
}

}  // namespace elog
