#include <algorithm>
#include <atomic>
#include <cinttypes>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>

#include "elog_api.h"
#include "elog_api_log_source.h"
#include "elog_api_log_target.h"
#include "elog_api_time_source.h"
#include "elog_cache.h"
#include "elog_common.h"
#include "elog_config.h"
#include "elog_config_loader.h"
#include "elog_config_parser.h"
#include "elog_field_selector_internal.h"
#include "elog_filter_internal.h"
#include "elog_flush_policy.h"
#include "elog_flush_policy_internal.h"
#include "elog_formatter_internal.h"
#include "elog_internal.h"
#include "elog_level_cfg.h"
#include "elog_pre_init_logger.h"
#include "elog_rate_limiter.h"
#include "elog_report.h"
#include "elog_schema_manager.h"
#include "elog_shared_logger.h"
#include "elog_stats_internal.h"
#include "elog_target_spec.h"
#include "elog_time_internal.h"
#include "elog_time_source.h"

#ifdef ELOG_ENABLE_MSG
#include "msg/elog_msg_internal.h"
#endif

#ifdef ELOG_ENABLE_LIFE_SIGN
#include "elog_api_life_sign.h"
#endif

#ifdef ELOG_ENABLE_RELOAD_CONFIG
#include "elog_api_reload_config.h"
#endif

#ifdef ELOG_ENABLE_CONFIG_SERVICE
#include "cfg_srv/elog_api_config_service.h"
#include "cfg_srv/elog_config_service_internal.h"
#endif

#ifdef ELOG_USING_DBG_UTIL
#include "elog_dbg_util_log_handler.h"
#endif

#ifdef ELOG_USING_COMM_UTIL
#include "elog_comm_util_log_handler.h"
#endif

#ifdef ELOG_ENABLE_STACK_TRACE
#include "elog_stack_trace.h"
#endif

// #include "elog_props_formatter.h"
namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELog)

#ifdef ELOG_USING_DBG_UTIL
static ELogDbgUtilLogHandler sDbgUtilLogHandler;
static bool sDbgUtilInitialized = false;
#endif

#ifdef ELOG_USING_COMM_UTIL
static ELogCommUtilLogHandler sCommUtilLogHandler;
static bool sCommUtilInitialized = false;
#endif

static bool sInitialized = false;
static bool sIsTerminating = false;
static ELogParams sParams;

static ELogPreInitLogger sPreInitLogger;
static ELogFilter* sGlobalFilter = nullptr;
static ELogLogger* sDefaultLogger = nullptr;
static ELogFormatter* sGlobalFormatter = nullptr;

static std::atomic<bool> sEnableStatistics = false;
static std::atomic<uint64_t> sMsgCount[ELEVEL_COUNT];

// these functions are defined as external because they are friends of some classes
extern bool initGlobals();
extern void termGlobals();
static void setReportLevelFromEnv();

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

    // initialize the time source if needed
    initTimeSource();

    // initialize log target statistics
    if (!initializeStats(sParams.m_maxThreads)) {
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

    if (!initLogFormatters()) {
        ELOG_REPORT_ERROR("Failed to initialize log formatters");
        termGlobals();
        return false;
    }
    ELOG_REPORT_TRACE("Log formatters initialized");

    if (!initLogSources()) {
        termGlobals();
        return false;
    }

    // create default logger
    sDefaultLogger = getRootLogSource()->createSharedLogger();
    if (sDefaultLogger == nullptr) {
        ELOG_REPORT_ERROR("Failed to create default logger, out of memory");
        termGlobals();
        return false;
    }
    ELOG_REPORT_TRACE("Default logger initialized");

    // create default target
    if (!initLogTargets()) {
        termGlobals();
        return false;
    }

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

#ifdef ELOG_USING_COMM_UTIL
    // connect to debug util library
    ELOG_REPORT_TRACE("Initializing Communication utility library");
    commutil::ErrorCode rc2 = commutil::initCommUtil(&sCommUtilLogHandler, commutil::LS_INFO);
    if (rc2 != commutil::ErrorCode::E_OK) {
        ELOG_REPORT_ERROR("Failed to initialize commutil library: %s",
                          commutil::errorCodeToString(rc2));
        termGlobals();
        return false;
    }
    sCommUtilLogHandler.applyLogLevelCfg();
    ELOG_REPORT_TRACE("Communication utility library logging initialized");
    sCommUtilInitialized = true;
#endif

#ifdef ELOG_ENABLE_STACK_TRACE
    ELOG_REPORT_TRACE("Initializing ELog stack trace services");
    initStackTrace();
    ELOG_REPORT_TRACE("ELog stack trace services initialized");
#endif

#ifdef ELOG_ENABLE_LIFE_SIGN
    // initialize life-sign report
    if (sParams.m_lifeSignParams.m_enableLifeSignReport) {
        ELOG_REPORT_TRACE("Initializing life-sign reports");
        if (!initLifeSignReport()) {
            termGlobals();
            return false;
        }
        ELOG_REPORT_TRACE("Life-sign report initialized");
    }
#endif

#ifdef ELOG_ENABLE_MSG
    if (!initBinaryFormatProviders()) {
        ELOG_REPORT_ERROR(
            "Failed to initialize binary format providers for log record serialization");
        termGlobals();
        return false;
    }
#endif

// must initialize static registration of config service publishers before loading configuration
#ifdef ELOG_ENABLE_CONFIG_SERVICE
    if (!initConfigServicePublishers()) {
        ELOG_REPORT_ERROR("Failed to initialize configuration service publishers");
        termGlobals();
        return false;
    }
#endif

    // load configuration from file
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

    // start the remote configuration service
#ifdef ELOG_ENABLE_CONFIG_SERVICE
    if (sParams.m_configServiceParams.m_enableConfigService) {
        if (!initConfigService()) {
            termGlobals();
            return false;
        }
    }
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

#ifdef ELOG_ENABLE_CONFIG_SERVICE
    if (sParams.m_configServiceParams.m_enableConfigService) {
        termConfigService();
        termConfigServicePublishers();
    }
#endif

#ifdef ELOG_ENABLE_RELOAD_CONFIG
    if (!sParams.m_configFilePath.empty() && sParams.m_reloadPeriodMillis > 0) {
        stopReloadConfigThread();
    }
#endif
    clearAllLogTargets();
    ELogReport::termReport();

#ifdef ELOG_ENABLE_MSG
    termBinaryFormatProviders();
#endif

#ifdef ELOG_ENABLE_LIFE_SIGN
    if (sParams.m_lifeSignParams.m_enableLifeSignReport) {
        if (!termLifeSignReport()) {
            ELOG_REPORT_ERROR("Failed to terminate life-sign reports");
            // continue anyway
        }
    }
#endif

#ifdef ELOG_USING_COMM_UTIL
    if (sCommUtilInitialized) {
        commutil::ErrorCode rc = commutil::termCommUtil();
        if (rc != commutil::ErrorCode::E_OK) {
            // issue error and continue
            ELOG_REPORT_ERROR("Failed to terminate Communication Util library: %s",
                              commutil::errorCodeToString(rc));
        }
        sCommUtilInitialized = false;
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
    termLogTargets();
    termLogSources();
    sDefaultLogger = nullptr;

    termLogFormatters();
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
    termTimeSource();
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

bool hasAccumulatedLogMessages() { return sPreInitLogger.hasAccumulatedLogMessages(); }

uint32_t getAccumulatedMessageCount(ELogFilter* filter /* = nullptr */) {
    return sPreInitLogger.getAccumulatedMessageCount(filter);
}

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

bool registerSchemaHandler(ELogSchemaHandler* schemaHandler) {
    return ELogSchemaManager::registerSchemaHandler(schemaHandler);
}

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

ELogLevel getLogLevel() { return getRootLogSource()->getLogLevel(); }

void setLogLevel(ELogLevel logLevel, ELogPropagateMode propagateMode) {
    return getRootLogSource()->setLogLevel(logLevel, propagateMode);
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
        destroyLogFormatter(logFormatter);
        return false;
    }
    setLogFormatter(logFormatter);
    return true;
}

void setLogFormatter(ELogFormatter* logFormatter) {
    if (sGlobalFormatter != nullptr) {
        destroyLogFormatter(sGlobalFormatter);
    }
    sGlobalFormatter = logFormatter;
}

const ELogParams& getParams() { return sParams; }

ELogParams& modifyParams() { return sParams; }

uint32_t getMaxThreads() { return sParams.m_maxThreads; }

bool isTerminating() { return sIsTerminating; }

ELogPreInitLogger& getPreInitLoggerRef() { return sPreInitLogger; }

#ifdef ELOG_USING_COMM_UTIL
void refreshCommUtilLogLevelCfg() { sCommUtilLogHandler.refreshLogLevelCfg(); }
#endif

void formatLogMsg(const ELogRecord& logRecord, std::string& logMsg) {
    sGlobalFormatter->formatLogMsg(logRecord, logMsg);
}

void formatLogBuffer(const ELogRecord& logRecord, ELogBuffer& logBuffer) {
    sGlobalFormatter->formatLogBuffer(logRecord, logBuffer);
}

ELogFormatter* getDefaultLogFormatter() { return sGlobalFormatter; }

ELogCacheEntryId cacheFormatMsg(const char* fmt) { return ELogCache::cacheFormatMsg(fmt); }

const char* getCachedFormatMsg(ELogCacheEntryId entryId) {
    return ELogCache::getCachedFormatMsg(entryId);
}

ELogCacheEntryId getOrCacheFormatMsg(const char* fmt) {
    return ELogCache::getOrCacheFormatMsg(fmt);
}

void setAppName(const char* appName) { setAppNameField(appName); }

const char* getAppName() { return getAppNameField(); }

bool setCurrentThreadName(const char* threadName) { return setCurrentThreadNameField(threadName); }

const char* getCurrentThreadName() { return getCurrentThreadNameField(); }

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
        destroyFilter(sGlobalFilter);
    }
    sGlobalFilter = logFilter;
}

bool setRateLimit(uint64_t maxMsg, uint64_t timeout, ELogTimeUnits timeoutUnits,
                  bool replaceGlobalFilter /* = true */) {
    ELogRateLimitFilter* rateLimiter =
        new (std::nothrow) ELogRateLimitFilter(maxMsg, timeout, timeoutUnits);
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
        destroyFilter(rateLimiter);
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
    if (sParams.m_lifeSignParams.m_enableLifeSignReport) {
        sendLifeSignReport(logRecord);
    }
#endif
    // send log record to all log targets
    logMsgTarget(logRecord, logTargetAffinityMask);

    // update global statistics
    if (sEnableStatistics.load(std::memory_order_relaxed)) {
        sMsgCount[logRecord.m_logLevel].fetch_add(1, std::memory_order_relaxed);
    }
}

void enableLogStatistics() { sEnableStatistics.store(true, std::memory_order_relaxed); }

void disableLogStatistics() { sEnableStatistics.store(false, std::memory_order_relaxed); }

void getLogStatistics(ELogStatistics& stats) {
    for (uint32_t i = 0; i < ELEVEL_COUNT; ++i) {
        stats.m_msgCount[i] = sMsgCount[i].load(std::memory_order_relaxed);
    }
}

void resetLogStatistics() {
    for (uint32_t i = 0; i < ELEVEL_COUNT; ++i) {
        sMsgCount[i].store(0, std::memory_order_relaxed);
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
