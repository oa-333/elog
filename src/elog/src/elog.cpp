#include "elog.h"

#include <algorithm>
#include <atomic>
#include <cinttypes>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <regex>
#include <unordered_map>

#include "elog_cache.h"
#include "elog_common.h"
#include "elog_config.h"
#include "elog_config_loader.h"
#include "elog_config_parser.h"
#include "elog_dbg_util_log_handler.h"
#include "elog_field_selector_internal.h"
#include "elog_filter_internal.h"
#include "elog_flush_policy.h"
#include "elog_flush_policy_internal.h"
#include "elog_internal.h"
#include "elog_level_cfg.h"
#include "elog_pre_init_logger.h"
#include "elog_rate_limiter.h"
#include "elog_report.h"
#include "elog_schema_manager.h"
#include "elog_shared_logger.h"
#include "elog_stack_trace.h"
#include "elog_stats_internal.h"
#include "elog_target_spec.h"
#include "elog_time_internal.h"
#include "file/elog_buffered_file_target.h"
#include "file/elog_file_schema_handler.h"
#include "file/elog_file_target.h"
#include "file/elog_segmented_file_target.h"
#include "sys/elog_syslog_target.h"
#include "sys/elog_win32_event_log_target.h"

#ifdef ELOG_ENABLE_LIFE_SIGN
#include "elog_gc.h"
#include "elog_life_sign_filter.h"
#include "elog_tls.h"
#include "life_sign_manager.h"
#include "os_thread_manager.h"
#endif

#ifdef ELOG_ENABLE_RELOAD_CONFIG
#include <condition_variable>
#include <thread>
#ifdef ELOG_LINUX
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif
#endif

// #include "elog_props_formatter.h"
namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELog)

#define ELOG_MAX_TARGET_COUNT 256ul

#ifdef ELOG_USING_DBG_UTIL
static ELogDbgUtilLogHandler sDbgUtilLogHandler;
static bool sDbgUtilInitialized = false;
#endif

static bool sInitialized = false;
static bool sIsTerminating = false;
static ELogParams sParams;
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
#ifdef ELOG_ENABLE_RELOAD_CONFIG
static std::thread sReloadConfigThread;
static std::mutex sReloadConfigLock;
static std::condition_variable sReloadConfigCV;
static bool sStopReloadConfig = false;
#endif

#ifdef ELOG_ENABLE_LIFE_SIGN
#define ELOG_LIFE_SIGN_APP_NAME_RECORD_ID 0
#define ELOG_LIFE_SIGN_THREAD_NAME_RECORD_ID 1
static ELogLifeSignFilter* sAppLifeSignFilter = nullptr;
static thread_local ELogLifeSignFilter* sThreadLifeSignFilter = nullptr;
static ELogTlsKey sThreadLifeSignKey = ELOG_INVALID_TLS_KEY;
static ELogGC* sLifeSignGC = nullptr;
static std::atomic<uint64_t> sLifeSignEpoch = 0;
static std::atomic<ELogFormatter*> sLifeSignFormatter = nullptr;
static uint64_t sLifeSignSyncPeriodMillis;
static std::thread sLifeSignSyncThread;
static std::mutex sLifeSignLock;
static std::condition_variable sLifeSignCV;
static bool initLifeSignReport();
static bool termLifeSignReport();
static void cleanupThreadLifeSignFilter(void* key);
static void sendLifeSignReport(const ELogRecord& logRecord);
static void getLogSources(const char* logSourceRegEx, std::vector<ELogSource*>& logSources);
#endif

// these functions are defined as external because they are friends of some classes
extern bool initGlobals();
extern void termGlobals();
static void setReportLevelFromEnv();
#ifdef ELOG_ENABLE_RELOAD_CONFIG
static void startReloadConfigThread();
static void stopReloadConfigThread();
static uint64_t getFileModifyTime(const char* filePath);
static bool reconfigure(ELogConfig* config);
#endif
extern ELogSource* createLogSource(ELogSourceId sourceId, const char* name,
                                   ELogSource* parent = nullptr, ELogLevel logLevel = ELEVEL_INFO);
extern void deleteLogSource(ELogSource* logSource);

// local helpers
static ELogSource* addChildSource(ELogSource* parent, const char* sourceName);
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

    // create default logger
    sDefaultLogger = sRootLogSource->createSharedLogger();
    if (sDefaultLogger == nullptr) {
        ELOG_REPORT_ERROR("Failed to create default logger, out of memory");
        termGlobals();
        return false;
    }
    ELOG_REPORT_TRACE("Default logger initialized");

    // create default target
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

#ifdef ELOG_USING_DBG_UTIL
    // connect to debug util library
    ELOG_REPORT_TRACE("Initializing Debug utility library");
    dbgutil::DbgUtilErr rc =
        dbgutil::initDbgUtil(nullptr, &sDbgUtilLogHandler, dbgutil::LS_INFO, DBGUTIL_FLAGS_ALL);
    if (rc != DBGUTIL_ERR_OK) {
        ELOG_REPORT_ERROR("Failed to initialize dbgutil library");
        termGlobals();
        return false;
    }
    sDbgUtilLogHandler.applyLogLevelCfg();
    ELOG_REPORT_TRACE("Debug utility library logging initialized");
    sDbgUtilInitialized = true;
#endif

#ifdef ELOG_ENABLE_STACK_TRACE
    ELOG_REPORT_TRACE("Initializing ELog stack trace services");
    initStackTrace();
    ELOG_REPORT_TRACE("ELog stack trace services initialized");
#endif

#ifdef ELOG_ENABLE_LIFE_SIGN
    // initialize life-sign report
    if (sParams.m_enableLifeSignReport) {
        ELOG_REPORT_TRACE("Initializing life-sign reports");
        if (!initLifeSignReport()) {
            termGlobals();
            return false;
        }
        ELOG_REPORT_TRACE("Life-sign report initialized");
    }
#endif

    if (!sParams.m_configFilePath.empty()) {
        ELOG_REPORT_TRACE("Loading configuration from: %s", sParams.m_configFilePath.c_str());
        if (!configureByFile(sParams.m_configFilePath.c_str())) {
            ELOG_REPORT_ERROR("Failed to load configuration from %s, ELog initialization aborted",
                              sParams.m_configFilePath.c_str());
            termGlobals();
            return false;
        }
        ELOG_REPORT_TRACE("Configuration loaded");
#ifdef ELOG_ENABLE_RELOAD_CONFIG
        if (sParams.m_reloadPeriodMillis > 0) {
            startReloadConfigThread();
        }
#endif
    }

    // now we can enable elog's self logger
    // any error up until now will be printed into stderr with no special formatting
    ELOG_REPORT_TRACE("Setting up ELog internal logger");
    ELogReport::initReport();

    ELOG_REPORT_INFO("ELog initialized successfully");
    return true;
}

void termGlobals() {
    sIsTerminating = true;
#ifdef ELOG_ENABLE_RELOAD_CONFIG
    if (!sParams.m_configFilePath.empty() && sParams.m_reloadPeriodMillis > 0) {
        stopReloadConfigThread();
    }
#endif
    clearAllLogTargets();
    ELogReport::termReport();

#ifdef ELOG_ENABLE_LIFE_SIGN
    if (sParams.m_enableLifeSignReport) {
        if (!termLifeSignReport()) {
            ELOG_REPORT_ERROR("Failed to terminate life-sign reports");
            // continue anyway
        }
    }
#endif

#ifdef ELOG_USING_DBG_UTIL
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

bool initialize(const ELogParams& params /* = ELogParams() */) {
    if (sInitialized) {
        ELOG_REPORT_ERROR("Duplicate attempt to initialize rejected");
        return false;
    }
    sParams = params;
    setReportHandler(params.m_reportHandler);
    setReportLevel(params.m_reportLevel);  // env setting can override this
    if (!initGlobals()) {
        return false;
    }
    sMaxThreads = params.m_maxThreads;
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

#ifdef ELOG_ENABLE_RELOAD_CONFIG
bool reloadConfigFile(const char* configPath /* = nullptr */) {
    // we reload only log levels and nothing else
    // future versions may allow adding log sources or log targets

    std::string usedConfigPath;
    if (configPath == nullptr) {
        std::unique_lock<std::mutex> lock(sReloadConfigLock);
        usedConfigPath = sParams.m_configFilePath;
    } else {
        usedConfigPath = configPath;
    }

    if (usedConfigPath.empty()) {
        ELOG_REPORT_ERROR(
            "Cannot reload configuration, no file path specified, and ELog was not initialized "
            "with a configuration file");
        return false;
    }

    ELogConfig* config = ELogConfig::loadFromFile(usedConfigPath.c_str());
    if (config == nullptr) {
        ELOG_REPORT_ERROR("Failed to reload configuration from file: %s", configPath);
        return false;
    }
    bool res = reconfigure(config);
    delete config;
    return res;
}

bool reloadConfigStr(const char* configStr) {
    // we reload only log levels and nothing else
    // future versions may allow adding log sources or log targets

    ELogConfig* config = ELogConfig::loadFromString(configStr);
    if (config == nullptr) {
        ELOG_REPORT_ERROR("Failed to reload configuration from string: %s", configStr);
        return false;
    }
    bool res = reconfigure(config);
    delete config;
    return res;
}

enum ReloadAction { START_RELOAD_THREAD, STOP_RELOAD_THREAD, NOTIFY_THREAD, NO_ACTION };

static bool execReloadAction(ReloadAction action, const char* configFilePath,
                             bool resetReloadPeriod = false) {
    if (action == START_RELOAD_THREAD) {
        ELOG_REPORT_TRACE("Loading configuration from: %s", configFilePath);
        if (!configureByFile(configFilePath)) {
            ELOG_REPORT_ERROR("Failed to load configuration from %s, ELog initialization aborted",
                              configFilePath);
            return false;
        }
        startReloadConfigThread();
    } else if (action == STOP_RELOAD_THREAD) {
        stopReloadConfigThread();
        if (resetReloadPeriod) {
            std::unique_lock<std::mutex> lock(sReloadConfigLock);
            sParams.m_reloadPeriodMillis = 0;
        }
    } else if (action == NOTIFY_THREAD) {
        sReloadConfigCV.notify_one();
    }

    return true;
}

bool setPeriodicReloadConfigFile(const char* configFilePath) {
    ReloadAction action = NO_ACTION;

    bool isEmptyFile = (configFilePath == nullptr || *configFilePath == 0);
    {
        std::unique_lock<std::mutex> lock(sReloadConfigLock);
        if (isEmptyFile) {
            if (sParams.m_configFilePath.empty()) {
                ELOG_REPORT_TRACE(
                    "Request to reset configuration reload file ignored, configuration file path "
                    "is already empty");
            } else {
                action = STOP_RELOAD_THREAD;
            }
        } else {
            if (sParams.m_configFilePath.empty()) {
                if (sParams.m_reloadPeriodMillis == 0) {
                    ELOG_REPORT_TRACE(
                        "Postponing launch of configuration reload thread until a reload period is "
                        "provided");
                } else {
                    action = START_RELOAD_THREAD;
                }
            } else {
                action = NOTIFY_THREAD;
            }
        }
        sParams.m_configFilePath = configFilePath;
    }

    return execReloadAction(action, configFilePath);
}

bool setReloadConfigPeriodMillis(uint64_t reloadPeriodMillis) {
    ReloadAction action = NO_ACTION;
    std::string configFilePath;

    {
        std::unique_lock<std::mutex> lock(sReloadConfigLock);
        if (reloadPeriodMillis == sParams.m_reloadPeriodMillis) {
            ELOG_REPORT_TRACE("Request to update configuration reload period to %" PRIu64
                              " milliseconds ignored, value is the same",
                              reloadPeriodMillis);
        } else if (sParams.m_reloadPeriodMillis == 0) {
            sParams.m_reloadPeriodMillis = reloadPeriodMillis;
            if (!sParams.m_configFilePath.empty()) {
                configFilePath = sParams.m_configFilePath;
                action = START_RELOAD_THREAD;
            } else {
                ELOG_REPORT_TRACE(
                    "Postponing launch of configuration reload thread until a configuration file "
                    "is provided");
            }
        } else {
            if (reloadPeriodMillis == 0) {
                // do not update period yet, otherwise config update thread might enter a tight loop
                action = STOP_RELOAD_THREAD;
            } else {
                sParams.m_reloadPeriodMillis = reloadPeriodMillis;
                action = NOTIFY_THREAD;
            }
        }
    }

    return execReloadAction(action, configFilePath.c_str(), true);
}

static bool shouldStopReloadConfig() {
    std::unique_lock<std::mutex> lock(sReloadConfigLock);
    return sStopReloadConfig;
}

void startReloadConfigThread() {
    // launch config reload thread
    sReloadConfigThread = std::thread([]() {
        setCurrentThreadNameField("reload-config");
        // get configuration file path (thread-safe, allowing concurrent updates)
        std::string configFilePath;
        {
            std::unique_lock<std::mutex> lock(sReloadConfigLock);
            configFilePath = sParams.m_configFilePath;
        }
        ELOG_REPORT_TRACE("Starting periodic configuration loading from %s, every %u milliseconds",
                          configFilePath.c_str(), sParams.m_reloadPeriodMillis);

        uint64_t lastFileModifyTime = getFileModifyTime(configFilePath.c_str());
        while (!shouldStopReloadConfig()) {
            // interruptible sleep until next reload check
            {
                std::unique_lock<std::mutex> lock(sReloadConfigLock);
                sReloadConfigCV.wait_for(lock,
                                         std::chrono::milliseconds(sParams.m_reloadPeriodMillis),
                                         []() { return sStopReloadConfig; });
                if (sStopReloadConfig) {
                    break;
                }

                // NOTE: still holding lock
                // update current config file path
                // if file changed then reset last modify time
                if (configFilePath.compare(sParams.m_configFilePath) != 0) {
                    lastFileModifyTime = 0;
                    configFilePath = sParams.m_configFilePath;
                }
            }

            uint64_t fileModifyTime = getFileModifyTime(configFilePath.c_str());
            if (fileModifyTime > lastFileModifyTime) {
                reloadConfigFile();
                lastFileModifyTime = fileModifyTime;
            }
        }
    });
}

void stopReloadConfigThread() {
    ELOG_REPORT_TRACE("Stopping periodic configuration loading thread");
    {
        std::unique_lock<std::mutex> lock(sReloadConfigLock);
        sStopReloadConfig = true;
        sReloadConfigCV.notify_one();
    }
    sReloadConfigThread.join();
    ELOG_REPORT_TRACE("Periodic configuration loading thread stopped");
}

uint64_t getFileModifyTime(const char* filePath) {
    // NOTE: we don't care that in each platform the time units are different (as well as the
    // reference epoch)
#ifdef ELOG_WINDOWS
    WIN32_FILE_ATTRIBUTE_DATA attrs = {};
    if (!GetFileAttributesExA(filePath, GetFileExInfoStandard, (LPVOID)&attrs)) {
        ELOG_REPORT_WIN32_ERROR(GetFileAttributesExA, "Failed to get file attributes for: %s",
                                filePath);
        return 0;
    }
    // return time in 100-nano units
    return static_cast<uint64_t>(attrs.ftLastWriteTime.dwHighDateTime) << 32 |
           attrs.ftLastWriteTime.dwLowDateTime;
#else
    struct stat fileStat;
    if (stat(filePath, &fileStat) == -1) {
        ELOG_REPORT_SYS_ERROR(stat, "Failed to get file status for: %s", filePath);
        return 0;
    }
    // return time in seconds
    return fileStat.st_mtime;
#endif
}
#endif  // ELOG_ENABLE_RELOAD_CONFIG

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

#ifdef ELOG_ENABLE_LIFE_SIGN
bool initLifeSignReport() {
    ELOG_REPORT_DEBUG("Creating life-sign shared memory segment");
    dbgutil::DbgUtilErr rc = dbgutil::getLifeSignManager()->createLifeSignShmSegment(
        DBGUTIL_MAX_CONTEXT_AREA_SIZE_BYTES, DBGUTIL_MAX_LIFE_SIGN_AREA_SIZE_BYTES, sMaxThreads,
        true);
    if (rc != DBGUTIL_ERR_OK) {
        ELOG_REPORT_ERROR("Failed to create life-sign segment for current process (error code: %u)",
                          (unsigned)rc);
        termLifeSignReport();
        return false;
    }

    // create application-scope filter
    sAppLifeSignFilter = new (std::nothrow) ELogLifeSignFilter();
    if (sAppLifeSignFilter == nullptr) {
        ELOG_REPORT_ERROR("Failed to allocate application-scope life sign filter, out of memory");
        return false;
    }

    // create garbage collector
    sLifeSignEpoch.store(0, std::memory_order_relaxed);
    sLifeSignGC = new (std::nothrow) ELogGC();
    if (sLifeSignGC == nullptr) {
        ELOG_REPORT_ERROR("Failed to allocate life sign report garbage collector, out of memory");
        termLifeSignReport();
        return false;
    }

    // initialize garbage collector
    if (!sLifeSignGC->initialize("elog_life_sign_gc", sMaxThreads, 0,
                                 sParams.m_lifeSignGCPeriodMillis, sParams.m_lifeSignGCTaskCount)) {
        ELOG_REPORT_ERROR("Failed to initialize life-sign report garbage collector");
        termLifeSignReport();
        return false;
    }

    // create TLS for thread-scope filter
    if (!elogCreateTls(sThreadLifeSignKey, cleanupThreadLifeSignFilter)) {
        ELOG_REPORT_ERROR("Failed to create thread local storage for life-sign filter");
        termLifeSignReport();
        return false;
    }

    return true;
}

bool termLifeSignReport() {
    // stop periodic syncing if any
    setLifeSignSyncPeriod(0);

    // delete formatter
    ELogFormatter* formatter = sLifeSignFormatter.load(std::memory_order_acquire);
    if (formatter != nullptr) {
        sLifeSignFormatter.store(nullptr, std::memory_order_release);
        delete formatter;
    }

    // destroy TLS for thread-scope life-sign reports
    if (sThreadLifeSignKey != ELOG_INVALID_TLS_KEY) {
        if (!elogDestroyTls(sThreadLifeSignKey)) {
            ELOG_REPORT_ERROR("Failed to destroy thread local storage for life-sign filter");
            return false;
        }
        sThreadLifeSignKey = ELOG_INVALID_TLS_KEY;
    }

    // terminate GC
    if (sLifeSignGC != nullptr) {
        if (!sLifeSignGC->destroy()) {
            ELOG_REPORT_ERROR("Failed to destroy life-sign reports garbage collector");
            return false;
        }
        delete sLifeSignGC;
        sLifeSignGC = nullptr;
    }

    // terminate application-scope filter
    if (sAppLifeSignFilter != nullptr) {
        delete sAppLifeSignFilter;
        sAppLifeSignFilter = nullptr;
    }

    // close shared memory segment and destroy it
    // (might consider external utility crond/service job to cleanup)
    dbgutil::DbgUtilErr rc = dbgutil::getLifeSignManager()->closeLifeSignShmSegment(true);
    if (rc != DBGUTIL_ERR_OK) {
        ELOG_REPORT_ERROR("Failed to destroy life-sign manager: %s", dbgutil::errorToString(rc));
        termLifeSignReport();
        return false;
    }

    return true;
}

void cleanupThreadLifeSignFilter(void* key) {
    ELogLifeSignFilter* filter = (ELogLifeSignFilter*)key;
    if (filter != nullptr) {
        delete filter;
    }
}

static ELogLifeSignFilter* initThreadLifeSignFilter() {
    ELogLifeSignFilter* filter = new (std::nothrow) ELogLifeSignFilter();
    if (filter == nullptr) {
        ELOG_REPORT_ERROR("Failed to allocate life-sign filter for current thread, out of memory");
        return nullptr;
    }
    if (!elogSetTls(sThreadLifeSignKey, filter)) {
        ELOG_REPORT_ERROR(
            "Failed to store life-sign filter for current thread in thread local storage");
        delete filter;
        filter = nullptr;
    }
    return filter;
}

static ELogLifeSignFilter* getThreadLifeSignFilter() {
    if (sThreadLifeSignFilter == nullptr) {
        sThreadLifeSignFilter = initThreadLifeSignFilter();
        if (sThreadLifeSignFilter == nullptr) {
            // put some invalid value different than null, signifying that the we should not try
            // to anymore to create a thread-local life-sign filter
            sThreadLifeSignFilter = (ELogLifeSignFilter*)-1;
        }
    }
    if (sThreadLifeSignFilter == (ELogLifeSignFilter*)-1) {
        return nullptr;
    }
    return sThreadLifeSignFilter;
}

static bool setAppLifeSignReport(ELogLevel level, const ELogFrequencySpec& frequencySpec,
                                 uint64_t currentEpoch) {
    // set filter
    ELogFilter* prevFilter = nullptr;
    if (!sAppLifeSignFilter->setLevelFilter(level, frequencySpec, prevFilter)) {
        ELOG_REPORT_ERROR("Failed to set application-scope life-sign report");
        return false;
    }

    // retire previous filter if any and finish
    if (prevFilter != nullptr) {
        sLifeSignGC->retire(prevFilter, currentEpoch);
    }
    return true;
}

static bool removeAppLifeSignReport(ELogLevel level, uint64_t currentEpoch) {
    // remove filter
    ELogFilter* prevFilter = sAppLifeSignFilter->removeLevelFilter(level);

    // retire previous filter if any and finish
    if (prevFilter != nullptr) {
        sLifeSignGC->retire(prevFilter, currentEpoch);
    }
    return true;
}

static bool setCurrentThreadLifeSignReport(ELogLevel level,
                                           const ELogFrequencySpec& frequencySpec) {
    ELogLifeSignFilter* threadFilter = getThreadLifeSignFilter();
    if (threadFilter == nullptr) {
        ELOG_REPORT_ERROR("Failed to retrieve current thread's life-sign filter");
        return false;
    }

    // set filter
    ELogFilter* prevFilter = nullptr;
    if (!threadFilter->setLevelFilter(level, frequencySpec, prevFilter)) {
        ELOG_REPORT_ERROR("Failed to set current thread's life-sign report");
        return false;
    }

    // no need for GC in case of current thread
    if (prevFilter != nullptr) {
        delete prevFilter;
    }
    return true;
}

static bool removeCurrentThreadLifeSignReport(ELogLevel level) {
    ELogLifeSignFilter* threadFilter = getThreadLifeSignFilter();
    if (threadFilter == nullptr) {
        ELOG_REPORT_ERROR("Failed to retrieve current thread's life-sign filter");
        return false;
    }

    // remove filter
    ELogFilter* prevFilter = threadFilter->removeLevelFilter(level);

    // no need for GC in case of current thread
    if (prevFilter != nullptr) {
        delete prevFilter;
    }
    return true;
}

static bool setThreadLifeSignReport(uint32_t threadId, const char* name, ELogLevel level,
                                    const ELogFrequencySpec& frequencySpec,
                                    dbgutil::ThreadNotifier* notifier) {
    dbgutil::DbgUtilErr requestResult = DBGUTIL_ERR_OK;
    dbgutil::ThreadWaitParams waitParams;
    waitParams.m_notifier = notifier;
    dbgutil::DbgUtilErr rc = dbgutil::execThreadRequest(
        threadId, requestResult, waitParams, [level, &frequencySpec]() -> dbgutil::DbgUtilErr {
            if (setCurrentThreadLifeSignReport(level, frequencySpec)) {
                return DBGUTIL_ERR_OK;
            } else {
                return DBGUTIL_ERR_SYSTEM_FAILURE;
            };
        });
    if (rc != DBGUTIL_ERR_OK) {
        ELOG_REPORT_ERROR("Failed to execute request on thread %u with name %s: %s", threadId, name,
                          dbgutil::errorToString(rc));
        return false;
    }
    if (requestResult != DBGUTIL_ERR_OK) {
        ELOG_REPORT_ERROR(
            "Attempt to set life-sign report on target thread %u with name %s failed: %s", threadId,
            name, dbgutil::errorToString(requestResult));
        return false;
    }
    return true;
}

static bool removeThreadLifeSignReport(uint32_t threadId, const char* name, ELogLevel level,
                                       dbgutil::ThreadNotifier* notifier) {
    dbgutil::DbgUtilErr requestResult = DBGUTIL_ERR_OK;
    dbgutil::ThreadWaitParams waitParams;
    waitParams.m_notifier = notifier;
    dbgutil::DbgUtilErr rc = dbgutil::execThreadRequest(
        threadId, requestResult, waitParams, [level]() -> dbgutil::DbgUtilErr {
            if (removeCurrentThreadLifeSignReport(level)) {
                return DBGUTIL_ERR_OK;
            } else {
                return DBGUTIL_ERR_SYSTEM_FAILURE;
            };
        });
    if (rc != DBGUTIL_ERR_OK) {
        ELOG_REPORT_ERROR("Failed to execute request on thread %u with name %s: %s", threadId, name,
                          dbgutil::errorToString(rc));
        return false;
    }
    if (requestResult != DBGUTIL_ERR_OK) {
        ELOG_REPORT_ERROR(
            "Attempt to remove life-sign report on target thread %u with name %s failed: %s",
            threadId, name, dbgutil::errorToString(requestResult));
        return false;
    }
    return true;
}

static bool setNamedThreadLifeSignReport(ELogLevel level, const ELogFrequencySpec& frequencySpec,
                                         const char* name) {
    uint32_t threadId = 0;
    dbgutil::ThreadNotifier* notifier = nullptr;
    if (!getThreadDataByName(name, threadId, notifier)) {
        ELOG_REPORT_WARN("Cannot set life-sign report, thread by name %s not found", name);
        return false;
    }
    return setThreadLifeSignReport(threadId, name, level, frequencySpec, notifier);
}

static bool removeNamedThreadLifeSignReport(ELogLevel level, const char* name) {
    uint32_t threadId = 0;
    dbgutil::ThreadNotifier* notifier = nullptr;
    if (!getThreadDataByName(name, threadId, notifier)) {
        ELOG_REPORT_WARN("Cannot remove life-sign report, thread by name %s not found", name);
        return false;
    }
    return removeThreadLifeSignReport(threadId, name, level, notifier);
}

static bool setThreadLifeSignReportByRegEx(ELogLevel level, const ELogFrequencySpec& frequencySpec,
                                           const char* nameRegEx) {
    ThreadDataMap threadData;
    getThreadDataByNameRegEx(nameRegEx, threadData);
    if (threadData.empty()) {
        ELOG_REPORT_WARN(
            "Cannot set life-sign report for threads with name %s regular expression, no thread "
            "was found matching this name",
            nameRegEx);
        return false;
    }

    bool res = true;
    for (const auto& entry : threadData) {
        if (!setThreadLifeSignReport(entry.first, entry.second.first.c_str(), level, frequencySpec,
                                     entry.second.second)) {
            res = false;
        }
    }
    return res;
}

static bool removeThreadLifeSignReportByRegEx(ELogLevel level, const char* nameRegEx) {
    ThreadDataMap threadData;
    getThreadDataByNameRegEx(nameRegEx, threadData);
    if (threadData.empty()) {
        ELOG_REPORT_WARN(
            "Cannot remove life-sign report for threads with name %s regular expression, no thread "
            "was found matching this name",
            nameRegEx);
        return false;
    }

    bool res = true;
    for (const auto& entry : threadData) {
        if (!removeThreadLifeSignReport(entry.first, entry.second.first.c_str(), level,
                                        entry.second.second)) {
            res = false;
        }
    }
    return res;
}

static bool setLogSourceLifeSignReport(ELogLevel level, const ELogFrequencySpec& frequencySpec,
                                       ELogSource* logSource, uint64_t currentEpoch) {
    // set filter
    ELogFilter* prevFilter = nullptr;
    if (!logSource->getLifeSignFilter()->setLevelFilter(level, frequencySpec, prevFilter)) {
        ELOG_REPORT_ERROR("Failed to set log source %s life-sign report",
                          logSource->getQualifiedName());
        return false;
    }

    // retire previous filter if any and finish
    if (prevFilter != nullptr) {
        sLifeSignGC->retire(prevFilter, currentEpoch);
    }
    return true;
}

static bool removeLogSourceLifeSignReport(ELogLevel level, ELogSource* logSource,
                                          uint64_t currentEpoch) {
    // remove filter
    ELogFilter* prevFilter = logSource->getLifeSignFilter()->removeLevelFilter(level);

    // retire previous filter if any and finish
    if (prevFilter != nullptr) {
        sLifeSignGC->retire(prevFilter, currentEpoch);
    }
    return true;
}

static bool setLogSourceLifeSignReport(ELogLevel level, const ELogFrequencySpec& frequencySpec,
                                       const char* name, uint64_t currentEpoch) {
    ELogSource* logSource = getLogSource(name);
    if (logSource == nullptr) {
        ELOG_REPORT_ERROR("Cannot set life-sign report for log source %s, log source not found",
                          name);
        return false;
    }
    return setLogSourceLifeSignReport(level, frequencySpec, logSource, currentEpoch);
}

static bool removeLogSourceLifeSignReport(ELogLevel level, const char* name,
                                          uint64_t currentEpoch) {
    ELogSource* logSource = getLogSource(name);
    if (logSource == nullptr) {
        ELOG_REPORT_ERROR("Cannot remove life-sign report for log source %s, log source not found",
                          name);
        return false;
    }
    return removeLogSourceLifeSignReport(level, logSource, currentEpoch);
}

static bool setLogSourceLifeSignReportByRegEx(ELogLevel level,
                                              const ELogFrequencySpec& frequencySpec,
                                              const char* nameRegEx, uint64_t currentEpoch) {
    std::vector<ELogSource*> logSources;
    getLogSources(nameRegEx, logSources);
    if (logSources.empty()) {
        ELOG_REPORT_ERROR(
            "Cannot set life report for log sources with reg-ex name %s, no log source "
            "matches the given name",
            nameRegEx);
        return false;
    }

    bool res = true;
    for (ELogSource* logSource : logSources) {
        if (!setLogSourceLifeSignReport(level, frequencySpec, logSource, currentEpoch)) {
            res = false;
        }
    }
    return res;
}

static bool removeLogSourceLifeSignReportByRegEx(ELogLevel level, const char* nameRegEx,
                                                 uint64_t currentEpoch) {
    std::vector<ELogSource*> logSources;
    getLogSources(nameRegEx, logSources);
    if (logSources.empty()) {
        ELOG_REPORT_ERROR(
            "Cannot remove life report for log sources with reg-ex name %s, no log source "
            "matches the given name",
            nameRegEx);
        return false;
    }

    bool res = true;
    for (ELogSource* logSource : logSources) {
        if (!removeLogSourceLifeSignReport(level, logSource, currentEpoch)) {
            res = false;
        }
    }
    return res;
}

bool setLifeSignReport(ELogLifeSignScope scope, ELogLevel level,
                       const ELogFrequencySpec& frequencySpec, const char* name /* = nullptr */,
                       bool isRegex /* = false */) {
    // increment epoch
    ELOG_SCOPED_EPOCH(sLifeSignGC, sLifeSignEpoch);

    // application scope
    if (scope == ELogLifeSignScope::LS_APP) {
        if (name != nullptr && *name != 0) {
            ELOG_REPORT_WARN("Ignoring name % sspecified for application-scope life-sign report",
                             name);
        }
        if (isRegex) {
            ELOG_REPORT_WARN(
                "Ignoring regular expression flag in application-scope life-sign report");
        }
        return setAppLifeSignReport(level, frequencySpec, ELOG_CURRENT_EPOCH);
    }

    // thread scope
    if (scope == ELogLifeSignScope::LS_THREAD) {
        // current thread
        if ((name == nullptr || *name == 0)) {
            if (isRegex) {
                ELOG_REPORT_WARN(
                    "Ignoring regular expression flag in current-thread-scope life-sign report");
            }
            return setCurrentThreadLifeSignReport(level, frequencySpec);
        } else {
            if (isRegex) {
                return setThreadLifeSignReportByRegEx(level, frequencySpec, name);
            } else {
                return setNamedThreadLifeSignReport(level, frequencySpec, name);
            }
        }
    }

    // log source scope
    if (scope == ELogLifeSignScope::LS_LOG_SOURCE) {
        if (isRegex) {
            return setLogSourceLifeSignReportByRegEx(level, frequencySpec, name,
                                                     ELOG_CURRENT_EPOCH);
        } else {
            return setLogSourceLifeSignReport(level, frequencySpec, name, ELOG_CURRENT_EPOCH);
        }
    }

    ELOG_REPORT_ERROR("Invalid life-sign report scope: %u", (uint32_t)scope);
    return false;
}

bool removeLifeSignReport(ELogLifeSignScope scope, ELogLevel level,
                          const char* name /* = nullptr */, bool isRegex /* = false */) {
    // increment epoch
    ELOG_SCOPED_EPOCH(sLifeSignGC, sLifeSignEpoch);

    // application scope
    if (scope == ELogLifeSignScope::LS_APP) {
        if (name != nullptr && *name != 0) {
            ELOG_REPORT_WARN(
                "Ignoring name % sspecified when removing application-scope life-sign report",
                name);
        }
        if (isRegex) {
            ELOG_REPORT_WARN(
                "Ignoring regular expression flag when removing application-scope life-sign "
                "report");
        }
        return removeAppLifeSignReport(level, ELOG_CURRENT_EPOCH);
    }

    // thread scope
    if (scope == ELogLifeSignScope::LS_THREAD) {
        // current thread
        if ((name == nullptr || *name == 0)) {
            if (isRegex) {
                ELOG_REPORT_WARN(
                    "Ignoring regular expression flag when removing current-thread-scope life-sign "
                    "report");
            }
            return removeCurrentThreadLifeSignReport(level);
        } else {
            if (isRegex) {
                return removeThreadLifeSignReportByRegEx(level, name);
            } else {
                return removeNamedThreadLifeSignReport(level, name);
            }
        }
    }

    // log source scope
    if (scope == ELogLifeSignScope::LS_LOG_SOURCE) {
        if (isRegex) {
            return removeLogSourceLifeSignReportByRegEx(level, name, ELOG_CURRENT_EPOCH);
        } else {
            return removeLogSourceLifeSignReport(level, name, ELOG_CURRENT_EPOCH);
        }
    }

    ELOG_REPORT_ERROR("Invalid life-sign report scope: %u", (uint32_t)scope);
    return false;
}

bool setLogSourceLifeSignReport(ELogLevel level, const ELogFrequencySpec& frequencySpec,
                                ELogSource* logSource) {
    // increment epoch and execute
    ELOG_SCOPED_EPOCH(sLifeSignGC, sLifeSignEpoch);
    return setLogSourceLifeSignReport(level, frequencySpec, logSource, ELOG_CURRENT_EPOCH);
}

bool removeLogSourceLifeSignReport(ELogLevel level, ELogSource* logSource) {
    // increment epoch and execute
    ELOG_SCOPED_EPOCH(sLifeSignGC, sLifeSignEpoch);
    return removeLogSourceLifeSignReport(level, logSource, ELOG_CURRENT_EPOCH);
}

bool setLifeSignLogFormat(const char* logFormat) {
    ELogFormatter* newFormatter = new (std::nothrow) ELogFormatter();
    if (newFormatter == nullptr) {
        ELOG_REPORT_ERROR("Failed to allocate life-sign log line formatter, out of memory");
        return false;
    }

    if (!newFormatter->initialize(logFormat)) {
        ELOG_REPORT_ERROR(
            "Failed to initialize life-sign log line formatter, invalid log line format: %s",
            logFormat);
        delete newFormatter;
        newFormatter = nullptr;
        return false;
    }

    // exchange pointers with much caution (not deleting, but retiring to GC)

    // first increment epoch
    uint64_t epoch = sLifeSignEpoch.fetch_add(1, std::memory_order_relaxed);
    sLifeSignGC->beginEpoch(epoch);

    // next, exchange pointers
    ELogFormatter* oldFormatter = sLifeSignFormatter.load(std::memory_order_acquire);
    sLifeSignFormatter.store(newFormatter, std::memory_order_release);

    // finally, retire the old formatter to GC and finish
    if (oldFormatter != nullptr) {
        sLifeSignGC->retire(oldFormatter, epoch);
    }
    sLifeSignGC->endEpoch(epoch);
    return true;
}

void setLifeSignSyncPeriod(uint64_t syncPeriodMillis) {
    // check if the timer thread is running, and if so update it's period, otherwise launch a new
    // timer thread
    std::unique_lock<std::mutex> lock(sLifeSignLock);
    sLifeSignSyncPeriodMillis = syncPeriodMillis;
    if (syncPeriodMillis > 0) {
        // create timer thread on-demand (no race, we have the lock)
        if (!sLifeSignSyncThread.joinable()) {
            sLifeSignSyncThread = std::thread([]() {
                setCurrentThreadNameField("life-sign-sync");
                std::unique_lock<std::mutex> threadLock(sLifeSignLock);
                while (sLifeSignSyncPeriodMillis > 0) {
                    sLifeSignCV.wait_for(threadLock,
                                         std::chrono::milliseconds(sLifeSignSyncPeriodMillis),
                                         []() { return true; });
                    syncLifeSignReport();
                }
            });
        } else {
            // otherwise just notify the thread about the change
            sLifeSignCV.notify_one();
        }
    } else {
        // notify thread about zero period, and wait for it to terminate
        if (sLifeSignSyncThread.joinable()) {
            sLifeSignCV.notify_one();
            lock.unlock();  // otherwise we dead-lock...
            sLifeSignSyncThread.join();
        }
    }
}

bool syncLifeSignReport() {
    dbgutil::DbgUtilErr rc = dbgutil::getLifeSignManager()->syncLifeSignShmSegment();
    if (rc != DBGUTIL_ERR_OK) {
        ELOG_REPORT_ERROR("Failed to synchronize life-sign report to disk (error code: %d)", rc);
        return false;
    }
    return true;
}

void reportLifeSign(const char* msg) {
    // reserve space for terminating null
    size_t len = strlen(msg) + 1;
    if (len > DBGUTIL_MAX_LIFE_SIGN_RECORD_SIZE_BYTES) {
        len = DBGUTIL_MAX_LIFE_SIGN_RECORD_SIZE_BYTES;
    }
    dbgutil::getLifeSignManager()->writeLifeSignRecord(msg, (uint32_t)len);
}

bool configureLifeSign(const char* lifeSignCfg) {
    ELogLifeSignScope scope = ELogLifeSignScope::LS_APP;
    ELogLevel level = ELEVEL_INFO;
    ELogFrequencySpec freqSpec(ELogFrequencySpecMethod::FS_EVERY_N_MESSAGES, 1);
    std::string name;
    bool removeCfg = false;
    if (!ELogConfigParser::parseLifeSignReport(lifeSignCfg, scope, level, freqSpec, name,
                                               removeCfg)) {
        ELOG_REPORT_ERROR("Cannot configure life-sign, invalid configuration: %s", lifeSignCfg);
        return false;
    }

    // NOTE: treating string as regular expression (if it is a simple string we will get the
    // correct result anyway)
    return removeCfg ? removeLifeSignReport(scope, level, name.c_str(), true)
                     : setLifeSignReport(scope, level, freqSpec, name.c_str(), true);
}

bool setCurrentThreadNotifier(dbgutil::ThreadNotifier* notifier) {
    return setCurrentThreadNotifierImpl(notifier);
}

bool setThreadNotifier(const char* threadName, dbgutil::ThreadNotifier* notifier) {
    return setThreadNotifierImpl(threadName, notifier);
}

void sendLifeSignReport(const ELogRecord& logRecord) {
    // increment epoch, so pointers are still valid
    ELOG_SCOPED_EPOCH(sLifeSignGC, sLifeSignEpoch);

    // first check life sign filter in log source
    bool sendReport = false;
    ELogLifeSignFilter* filter = logRecord.m_logger->getLogSource()->getLifeSignFilter();
    if (filter->hasLevelFilter(logRecord.m_logLevel)) {
        sendReport = filter->filterLogRecord(logRecord);
    }

    // check thread local filter
    if (!sendReport) {
        filter = getThreadLifeSignFilter();
        if (filter != nullptr) {
            if (filter->hasLevelFilter(logRecord.m_logLevel)) {
                sendReport = filter->filterLogRecord(logRecord);
            }
        }
    }

    // check application-scope filter
    if (!sendReport) {
        if (sAppLifeSignFilter->hasLevelFilter(logRecord.m_logLevel)) {
            sendReport = sAppLifeSignFilter->filterLogRecord(logRecord);
        }
    }

    if (sendReport) {
        // format log line
        ELogFormatter* formatter = sLifeSignFormatter.load(std::memory_order_relaxed);
        if (formatter == nullptr) {
            formatter = sGlobalFormatter;
        }
        ELogBuffer logBuffer;
        formatter->formatLogBuffer(logRecord, logBuffer);
        logBuffer.finalize();
        // NOTE: offset points to terminating null
        dbgutil::DbgUtilErr rc = dbgutil::getLifeSignManager()->writeLifeSignRecord(
            logBuffer.getRef(), (uint32_t)logBuffer.getOffset());
        if (rc != DBGUTIL_ERR_OK) {
            ELOG_REPORT_ERROR("Failed to write life sign record: %s", dbgutil::errorToString(rc));
        }
    }
}

void reportAppNameLifeSign(const char* appName) {
    // reserve space for terminating null
    size_t nameLen = strlen(appName) + 1;
    size_t totalLen = nameLen + sizeof(uint32_t);
    if (totalLen > DBGUTIL_MAX_CONTEXT_RECORD_SIZE_BYTES) {
        // save 4 bytes for record type, 1 byte for terminating null
        totalLen = DBGUTIL_MAX_CONTEXT_RECORD_SIZE_BYTES;
        nameLen = totalLen - sizeof(uint32_t) - 1;
    }
    char* buf = new (std::nothrow) char[totalLen];
    if (buf == nullptr) {
        ELOG_REPORT_ERROR(
            "Failed to allocate %u bytes for life-sign application name context record", totalLen);
        return;
    }
    *(uint32_t*)(buf) = ELOG_LIFE_SIGN_APP_NAME_RECORD_ID;
    uint32_t offset = sizeof(uint32_t);
    memcpy(buf + offset, appName, nameLen);
    // NOTE: cast is safe after verifying totalLen above
    dbgutil::DbgUtilErr rc =
        dbgutil::getLifeSignManager()->writeContextRecord(buf, (uint32_t)totalLen);
    if (rc != DBGUTIL_ERR_OK) {
        ELOG_REPORT_ERROR("Failed to write life-sign application name context record: %s",
                          dbgutil::errorToString(rc));
    }
    delete[] buf;
}

void reportCurrentThreadNameLifeSign(elog_thread_id_t threadId, const char* threadName) {
    size_t nameLen = strlen(threadName) + 1;
    uint32_t totalLen = (uint32_t)(nameLen + sizeof(uint32_t) + sizeof(uint64_t));
    if (nameLen > DBGUTIL_MAX_LIFE_SIGN_RECORD_SIZE_BYTES) {
        // save 4 bytes for record type, 8 bytes for thread id, 1 byte for terminating null
        nameLen = DBGUTIL_MAX_LIFE_SIGN_RECORD_SIZE_BYTES - sizeof(uint32_t) + sizeof(uint64_t) - 1;
    }
    char* buf = new (std::nothrow) char[totalLen];
    if (buf == nullptr) {
        ELOG_REPORT_ERROR("Failed to allocate %u bytes for life-sign thread name context record",
                          totalLen);
        return;
    }
    *(uint32_t*)(buf) = ELOG_LIFE_SIGN_THREAD_NAME_RECORD_ID;
    uint32_t offset = sizeof(uint32_t);
    *(uint64_t*)(buf + offset) = (uint64_t)threadId;
    offset += sizeof(uint64_t);
    memcpy(buf + offset, threadName, nameLen);
    dbgutil::DbgUtilErr rc = dbgutil::getLifeSignManager()->writeContextRecord(buf, totalLen);
    if (rc != DBGUTIL_ERR_OK) {
        ELOG_REPORT_ERROR("Failed to write life-sign current thread name context record: %s",
                          dbgutil::errorToString(rc));
    }
    delete[] buf;
}

void getLogSources(const char* logSourceRegEx, std::vector<ELogSource*>& logSources) {
    std::regex pattern(logSourceRegEx);
    std::unique_lock<std::mutex> lock(sSourceTreeLock);
    ELogSourceMap::iterator itr = sLogSourceMap.begin();
    while (itr != sLogSourceMap.end()) {
        if (std::regex_match(itr->second->getQualifiedName(), pattern)) {
            logSources.emplace_back(itr->second);
        }
        ++itr;
    }
}
#endif

bool configureRateLimit(const char* rateLimitCfg, bool replaceGlobalFilter /* = true */) {
    uint64_t maxMsg = 0;
    uint64_t timeout = 0;
    ELogTimeUnits units = ELogTimeUnits::TU_NONE;
    // parse <max-msg>:<timeout>:<units>
    if (!ELogConfigParser::parseRateLimit(rateLimitCfg, maxMsg, timeout, units)) {
        ELOG_REPORT_ERROR("Failed to parse rate limit configuration: ", rateLimitCfg);
        return false;
    }
    return setRateLimit(maxMsg, timeout, units, replaceGlobalFilter);
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
        if (!configureRateLimit(rateLimitCfg.c_str())) {
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

// configure life-sign report settings
#ifdef ELOG_ENABLE_LIFE_SIGN
    std::vector<std::string> lifeSignCfgArray;
    getPropsByPrefix(props, ELOG_LIFE_SIGN_REPORT_CONFIG_NAME, lifeSignCfgArray);
    for (const auto& lifeSignCfg : lifeSignCfgArray) {
        if (!configureLifeSign(lifeSignCfg.c_str())) {
            return false;
        }
    }

    std::string lifeSignLogFormat;
    if (getProp(props, ELOG_LIFE_SIGN_LOG_FORMAT_CONFIG_NAME, lifeSignLogFormat)) {
        if (!setLifeSignLogFormat(lifeSignLogFormat.c_str())) {
            return false;
        }
    }

    std::string lifeSignSyncPeriodStr;
    if (getProp(props, ELOG_LIFE_SIGN_SYNC_PERIOD_CONFIG_NAME, lifeSignSyncPeriodStr)) {
        uint64_t syncPeriodMillis = 0;
        ELogTimeUnits origUnits = ELogTimeUnits::TU_NONE;
        if (!parseTimeValueProp(ELOG_LIFE_SIGN_SYNC_PERIOD_CONFIG_NAME, "", lifeSignSyncPeriodStr,
                                syncPeriodMillis, origUnits, ELogTimeUnits::TU_MILLI_SECONDS)) {
            ELOG_REPORT_ERROR("Invalid life-sign synchronization period configuration: %s",
                              lifeSignSyncPeriodStr.c_str());
            return false;
        }
        setLifeSignSyncPeriod(syncPeriodMillis);
    }
#endif

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
    ELogConfigMapNode* cfgMap =
        const_cast<ELogConfigMapNode*>((const ELogConfigMapNode*)config->getRootNode());

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
    std::string rateLimitCfg;
    if (!cfgMap->getStringValue(ELOG_RATE_LIMIT_CONFIG_NAME, found, rateLimitCfg)) {
        // configuration error
        return false;
    } else if (found && !configureRateLimit(rateLimitCfg.c_str())) {
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
            const char* logLevelStr = ((const ELogConfigStringValue*)cfgValue)->getStringValue();
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
                const char* logTargetStr =
                    ((const ELogConfigStringValue*)cfgValue)->getStringValue();
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
            const char* logLevelStr = ((const ELogConfigStringValue*)cfgValue)->getStringValue();
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
            const char* logAffinityStr = ((const ELogConfigStringValue*)cfgValue)->getStringValue();
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

// configure life-sign report settings
#ifdef ELOG_ENABLE_LIFE_SIGN
    const ELogConfigValue* cfgValue = cfgMap->getValue(ELOG_LIFE_SIGN_REPORT_CONFIG_NAME);
    if (cfgValue != nullptr) {
        if (cfgValue->getValueType() != ELogConfigValueType::ELOG_CONFIG_ARRAY_VALUE) {
            ELOG_REPORT_ERROR("Invalid type for %s, expecting array",
                              ELOG_LIFE_SIGN_REPORT_CONFIG_NAME);
            return false;
        }
        const ELogConfigArrayNode* arrayNode =
            ((const ELogConfigArrayValue*)cfgValue)->getArrayNode();
        for (uint32_t i = 0; i < arrayNode->getValueCount(); ++i) {
            const ELogConfigValue* subValue = arrayNode->getValueAt(i);
            if (subValue->getValueType() != ELogConfigValueType::ELOG_CONFIG_STRING_VALUE) {
                ELOG_REPORT_ERROR(
                    "Invalid type for %uth sub-element in life-sign report array, expecting "
                    "string, "
                    "got instead %s",
                    i, configValueTypeToString(subValue->getValueType()));
                return false;
            }
            const char* lifeSignCfg = ((const ELogConfigStringValue*)subValue)->getStringValue();
            if (!configureLifeSign(lifeSignCfg)) {
                return false;
            }
        }
    }

    std::string lifeSignLogFormat;
    if (!cfgMap->getStringValue(ELOG_LIFE_SIGN_LOG_FORMAT_CONFIG_NAME, found, lifeSignLogFormat)) {
        return false;
    }
    if (found && !setLifeSignLogFormat(lifeSignLogFormat.c_str())) {
        return false;
    }

    std::string lifeSignSyncPeriodStr;
    if (!cfgMap->getStringValue(ELOG_LIFE_SIGN_SYNC_PERIOD_CONFIG_NAME, found,
                                lifeSignSyncPeriodStr)) {
        return false;
    }
    if (found) {
        uint64_t syncPeriodMillis = 0;
        ELogTimeUnits origUnits = ELogTimeUnits::TU_NONE;
        if (!parseTimeValueProp(ELOG_LIFE_SIGN_SYNC_PERIOD_CONFIG_NAME, "", lifeSignSyncPeriodStr,
                                syncPeriodMillis, origUnits, ELogTimeUnits::TU_MILLI_SECONDS)) {
            ELOG_REPORT_ERROR("Invalid life-sign synchronization period configuration: %s",
                              lifeSignSyncPeriodStr.c_str());
            return false;
        }
        setLifeSignSyncPeriod(syncPeriodMillis);
    }
#endif

    return true;
}

// TODO: refactor reused code
#ifdef ELOG_ENABLE_RELOAD_CONFIG
static bool reconfigure(ELogConfig* config) {
    // verify root node is of map type
    if (config->getRootNode()->getNodeType() != ELogConfigNodeType::ELOG_CONFIG_MAP_NODE) {
        ELOG_REPORT_ERROR("Top-level configuration node is not a map node");
        return false;
    }
    const ELogConfigMapNode* cfgMap = (const ELogConfigMapNode*)config->getRootNode();

    std::vector<ELogLevelCfg> logLevelCfg;
    ELogLevel logLevel = ELEVEL_INFO;
    ELogPropagateMode propagateMode = ELogPropagateMode::PM_NONE;

    const char* logLevelSuffix = "." ELOG_LEVEL_CONFIG_NAME;
    size_t logLevelSuffixLen = strlen(logLevelSuffix);

    for (size_t i = 0; i < cfgMap->getEntryCount(); ++i) {
        const ELogConfigMapNode::EntryType& prop = cfgMap->getEntryAt(i);
        const ELogConfigValue* cfgValue = prop.second;
        // check if this is root log level
        if (prop.first.compare(ELOG_LEVEL_CONFIG_NAME) == 0) {
            // global log level, should be a string
            if (!validateConfigValueStringType(cfgValue, ELOG_LEVEL_CONFIG_NAME)) {
                return false;
            }
            const char* logLevelStr = ((const ELogConfigStringValue*)cfgValue)->getStringValue();
            if (!ELogConfigParser::parseLogLevel(logLevelStr, logLevel, propagateMode)) {
                ELOG_REPORT_ERROR("Invalid global log level: %s", logLevelStr);
                return false;
            }
            // configure later when we have all information gathered
            logLevelCfg.push_back({getRootLogSource(), logLevel, propagateMode});
            continue;
        }

        // configure log levels of log sources
        // search for ".log_level" with a dot, in order to filter out global log_level key
        if (prop.first.ends_with(logLevelSuffix)) {
            const std::string& key = prop.first;
            // shave off trailing ".log_level/_log_level" (that includes dot/underscore)
            std::string sourceName = key.substr(0, key.size() - logLevelSuffixLen);
            ELogSource* logSource = getLogSource(sourceName.c_str());
            if (logSource == nullptr) {
                ELOG_REPORT_ERROR("Invalid log source name: %s", sourceName.c_str());
                return false;
            }
            propagateMode = ELogPropagateMode::PM_NONE;
            // get log level, should be a string
            if (!validateConfigValueStringType(cfgValue, key.c_str())) {
                return false;
            }
            const char* logLevelStr = ((const ELogConfigStringValue*)cfgValue)->getStringValue();
            if (!ELogConfigParser::parseLogLevel(logLevelStr, logLevel, propagateMode)) {
                ELOG_REPORT_ERROR("Invalid source %s log level: %s", sourceName.c_str(),
                                  logLevelStr);
                return false;
            }
            logLevelCfg.push_back({logSource, logLevel, propagateMode});
        }
    }

    // now we can apply log level propagation
    for (const ELogLevelCfg& cfg : logLevelCfg) {
        ELOG_REPORT_TRACE("Setting %s log level to %s (propagate - %u)",
                          cfg.m_logSource->getQualifiedName(), elogLevelToStr(cfg.m_logLevel),
                          (uint32_t)cfg.m_propagationMode);
        cfg.m_logSource->setLogLevel(cfg.m_logLevel, cfg.m_propagationMode);
    }

#ifdef ELOG_ENABLE_LIFE_SIGN
    // configure life-sign report settings
    const ELogConfigValue* cfgValue = cfgMap->getValue(ELOG_LIFE_SIGN_REPORT_CONFIG_NAME);
    if (cfgValue != nullptr) {
        if (cfgValue->getValueType() != ELogConfigValueType::ELOG_CONFIG_ARRAY_VALUE) {
            ELOG_REPORT_ERROR("Invalid type for %s, expecting array",
                              ELOG_LIFE_SIGN_REPORT_CONFIG_NAME);
            return false;
        }
        const ELogConfigArrayNode* arrayNode =
            ((const ELogConfigArrayValue*)cfgValue)->getArrayNode();
        for (uint32_t i = 0; i < arrayNode->getValueCount(); ++i) {
            const ELogConfigValue* subValue = arrayNode->getValueAt(i);
            if (subValue->getValueType() != ELogConfigValueType::ELOG_CONFIG_STRING_VALUE) {
                ELOG_REPORT_ERROR(
                    "Invalid type for %uth sub-element in life-sign report array, expecting "
                    "string, "
                    "got instead %s",
                    i, configValueTypeToString(subValue->getValueType()));
                return false;
            }
            const char* lifeSignCfg = ((const ELogConfigStringValue*)subValue)->getStringValue();
            if (!configureLifeSign(lifeSignCfg)) {
                return false;
            }
        }
    }
#endif

    return true;
}
#endif

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
    if (logSource == nullptr) {
        ELOG_REPORT_ERROR("Failed to create log source %s, out of memory", sourceName);
        return nullptr;
    }
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
        ELogStats* stats = logTarget->getStats();
        if (stats != nullptr) {
            stats->resetThreadCounters(slotId);
        }
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

bool setCurrentThreadName(const char* threadName) { return setCurrentThreadNameField(threadName); }

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

bool setRateLimit(uint64_t maxMsg, uint64_t timeout, ELogTimeUnits timeoutUnits,
                  bool replaceGlobalFilter /* = true */) {
    ELogRateLimiter* rateLimiter =
        new (std::nothrow) ELogRateLimiter(maxMsg, timeout, timeoutUnits);
    if (rateLimiter == nullptr) {
        ELOG_REPORT_ERROR("Failed to set rate limit, out of memory");
        return false;
    }
    if (sGlobalFilter == nullptr || replaceGlobalFilter) {
        setLogFilter(rateLimiter);
    }

    // if a global log filter already exists, then the rate limiter should be added using AND filter
    // before the existing filter
    ELogAndLogFilter* logFilter = new (std::nothrow) ELogAndLogFilter();
    if (logFilter == nullptr) {
        ELOG_REPORT_ERROR(
            "Failed to allocate AND log filter for global rate limiter, out of memory");
        delete rateLimiter;
        return false;
    }
    // put rate limiter first, and if ok, then apply next filter
    logFilter->addFilter(rateLimiter);
    logFilter->addFilter(sGlobalFilter);
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
    void onStackEntry(const char* stackEntry) override {
        ELOG_APPEND_EX(m_logger, m_logLevel, "%s\n", stackEntry);
    }

private:
    ELogLogger* m_logger;
    ELogLevel m_logLevel;
    std::string m_title;
};

void logStackTrace(ELogLogger* logger, ELogLevel logLevel, const char* title, int skip,
                   dbgutil::StackEntryFormatter* formatter) {
    // ELogStackEntryFilter filter;
    LogStackEntryPrinter printer(logger, logLevel, title == nullptr ? "" : title);
    dbgutil::printStackTrace(skip, nullptr /* &filter */, formatter, &printer);
}

void logStackTraceContext(ELogLogger* logger, void* context, ELogLevel logLevel, const char* title,
                          int skip, dbgutil::StackEntryFormatter* formatter) {
    // ELogStackEntryFilter filter;
    LogStackEntryPrinter printer(logger, logLevel, title == nullptr ? "" : title);
    dbgutil::printStackTraceContext(context, skip, nullptr /* &filter */, formatter, &printer);
}

void logAppStackTrace(ELogLogger* logger, ELogLevel logLevel, const char* title, int skip,
                      dbgutil::StackEntryFormatter* formatter) {
    // ELogStackEntryFilter filter;
    LogStackEntryPrinter printer(logger, logLevel, title == nullptr ? "" : title);
    dbgutil::printAppStackTrace(skip, nullptr /* &filter */, formatter, &printer);
}
#endif

void logMsg(const ELogRecord& logRecord,
            ELogTargetAffinityMask logTargetAffinityMask /* = ELOG_ALL_TARGET_AFFINITY_MASK */) {
#ifdef ELOG_ENABLE_LIFE_SIGN
    if (sParams.m_enableLifeSignReport) {
        sendLifeSignReport(logRecord);
    }
#endif
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
