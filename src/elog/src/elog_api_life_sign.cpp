#include "elog_api_life_sign.h"

#ifdef ELOG_ENABLE_LIFE_SIGN

#include "elog_api.h"
#include "elog_config_parser.h"
#include "elog_field_selector_internal.h"
#include "elog_formatter.h"
#include "elog_gc.h"
#include "elog_internal.h"
#include "elog_life_sign_filter.h"
#include "elog_report.h"
#include "elog_tls.h"
#include "life_sign_manager.h"
#include "os_thread_manager.h"

#define ELOG_LIFE_SIGN_APP_NAME_RECORD_ID 0
#define ELOG_LIFE_SIGN_THREAD_NAME_RECORD_ID 1

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogLifeSignApi)

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

static void cleanupThreadLifeSignFilter(void* key);
static ELogLifeSignFilter* initThreadLifeSignFilter();
static ELogLifeSignFilter* getThreadLifeSignFilter();
static bool setAppLifeSignReport(ELogLevel level, const ELogFrequencySpec& frequencySpec,
                                 uint64_t currentEpoch);
static bool removeAppLifeSignReport(ELogLevel level, uint64_t currentEpoch);
static bool setCurrentThreadLifeSignReport(ELogLevel level, const ELogFrequencySpec& frequencySpec);
static bool removeCurrentThreadLifeSignReport(ELogLevel level);
static bool setThreadLifeSignReport(uint32_t threadId, const char* name, ELogLevel level,
                                    const ELogFrequencySpec& frequencySpec,
                                    dbgutil::ThreadNotifier* notifier);
static bool removeThreadLifeSignReport(uint32_t threadId, const char* name, ELogLevel level,
                                       dbgutil::ThreadNotifier* notifier);
static bool setNamedThreadLifeSignReport(ELogLevel level, const ELogFrequencySpec& frequencySpec,
                                         const char* name);
static bool removeNamedThreadLifeSignReport(ELogLevel level, const char* name);
static bool setThreadLifeSignReportByRegEx(ELogLevel level, const ELogFrequencySpec& frequencySpec,
                                           const char* nameRegEx);
static bool removeThreadLifeSignReportByRegEx(ELogLevel level, const char* nameRegEx);
static bool setLogSourceLifeSignReport(ELogLevel level, const ELogFrequencySpec& frequencySpec,
                                       ELogSource* logSource, uint64_t currentEpoch);
static bool removeLogSourceLifeSignReport(ELogLevel level, ELogSource* logSource,
                                          uint64_t currentEpoch);
static bool setLogSourceLifeSignReport(ELogLevel level, const ELogFrequencySpec& frequencySpec,
                                       const char* name, uint64_t currentEpoch);
static bool removeLogSourceLifeSignReport(ELogLevel level, const char* name, uint64_t currentEpoch);
static bool setLogSourceLifeSignReportByRegEx(ELogLevel level,
                                              const ELogFrequencySpec& frequencySpec,
                                              const char* nameRegEx, uint64_t currentEpoch);
static bool removeLogSourceLifeSignReportByRegEx(ELogLevel level, const char* nameRegEx,
                                                 uint64_t currentEpoch);

bool initLifeSignReport() {
    ELOG_REPORT_DEBUG("Creating life-sign shared memory segment");
    dbgutil::DbgUtilErr rc = dbgutil::getLifeSignManager()->createLifeSignShmSegment(
        DBGUTIL_MAX_CONTEXT_AREA_SIZE_BYTES, DBGUTIL_MAX_LIFE_SIGN_AREA_SIZE_BYTES, getMaxThreads(),
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
    if (!sLifeSignGC->initialize("elog_life_sign_gc", getMaxThreads(), 0,
                                 getParams().m_lifeSignParams.m_lifeSignGCPeriodMillis,
                                 getParams().m_lifeSignParams.m_lifeSignGCTaskCount)) {
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
        destroyLogFormatter(formatter);
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

ELogLifeSignFilter* initThreadLifeSignFilter() {
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

ELogLifeSignFilter* getThreadLifeSignFilter() {
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

bool setAppLifeSignReport(ELogLevel level, const ELogFrequencySpec& frequencySpec,
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

bool removeAppLifeSignReport(ELogLevel level, uint64_t currentEpoch) {
    // remove filter
    ELogFilter* prevFilter = sAppLifeSignFilter->removeLevelFilter(level);

    // retire previous filter if any and finish
    if (prevFilter != nullptr) {
        sLifeSignGC->retire(prevFilter, currentEpoch);
    }
    return true;
}

bool setCurrentThreadLifeSignReport(ELogLevel level, const ELogFrequencySpec& frequencySpec) {
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
        destroyFilter(prevFilter);
    }
    return true;
}

bool removeCurrentThreadLifeSignReport(ELogLevel level) {
    ELogLifeSignFilter* threadFilter = getThreadLifeSignFilter();
    if (threadFilter == nullptr) {
        ELOG_REPORT_ERROR("Failed to retrieve current thread's life-sign filter");
        return false;
    }

    // remove filter
    ELogFilter* prevFilter = threadFilter->removeLevelFilter(level);

    // no need for GC in case of current thread
    if (prevFilter != nullptr) {
        destroyFilter(prevFilter);
    }
    return true;
}

bool setThreadLifeSignReport(uint32_t threadId, const char* name, ELogLevel level,
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

bool removeThreadLifeSignReport(uint32_t threadId, const char* name, ELogLevel level,
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

bool setNamedThreadLifeSignReport(ELogLevel level, const ELogFrequencySpec& frequencySpec,
                                  const char* name) {
    uint32_t threadId = 0;
    dbgutil::ThreadNotifier* notifier = nullptr;
    if (!getThreadDataByName(name, threadId, notifier)) {
        ELOG_REPORT_WARN("Cannot set life-sign report, thread by name %s not found", name);
        return false;
    }
    return setThreadLifeSignReport(threadId, name, level, frequencySpec, notifier);
}

bool removeNamedThreadLifeSignReport(ELogLevel level, const char* name) {
    uint32_t threadId = 0;
    dbgutil::ThreadNotifier* notifier = nullptr;
    if (!getThreadDataByName(name, threadId, notifier)) {
        ELOG_REPORT_WARN("Cannot remove life-sign report, thread by name %s not found", name);
        return false;
    }
    return removeThreadLifeSignReport(threadId, name, level, notifier);
}

bool setThreadLifeSignReportByRegEx(ELogLevel level, const ELogFrequencySpec& frequencySpec,
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

bool removeThreadLifeSignReportByRegEx(ELogLevel level, const char* nameRegEx) {
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

bool setLogSourceLifeSignReport(ELogLevel level, const ELogFrequencySpec& frequencySpec,
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

bool removeLogSourceLifeSignReport(ELogLevel level, ELogSource* logSource, uint64_t currentEpoch) {
    // remove filter
    ELogFilter* prevFilter = logSource->getLifeSignFilter()->removeLevelFilter(level);

    // retire previous filter if any and finish
    if (prevFilter != nullptr) {
        sLifeSignGC->retire(prevFilter, currentEpoch);
    }
    return true;
}

bool setLogSourceLifeSignReport(ELogLevel level, const ELogFrequencySpec& frequencySpec,
                                const char* name, uint64_t currentEpoch) {
    ELogSource* logSource = getLogSource(name);
    if (logSource == nullptr) {
        ELOG_REPORT_ERROR("Cannot set life-sign report for log source %s, log source not found",
                          name);
        return false;
    }
    return setLogSourceLifeSignReport(level, frequencySpec, logSource, currentEpoch);
}

bool removeLogSourceLifeSignReport(ELogLevel level, const char* name, uint64_t currentEpoch) {
    ELogSource* logSource = getLogSource(name);
    if (logSource == nullptr) {
        ELOG_REPORT_ERROR("Cannot remove life-sign report for log source %s, log source not found",
                          name);
        return false;
    }
    return removeLogSourceLifeSignReport(level, logSource, currentEpoch);
}

bool setLogSourceLifeSignReportByRegEx(ELogLevel level, const ELogFrequencySpec& frequencySpec,
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

bool removeLogSourceLifeSignReportByRegEx(ELogLevel level, const char* nameRegEx,
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
        destroyLogFormatter(newFormatter);
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
            formatter = getDefaultLogFormatter();
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

bool configLifeSignBasic(const ELogConfigMapNode* cfgMap) {
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
                    "string, got instead %s",
                    i, configValueTypeToString(subValue->getValueType()));
                return false;
            }
            const char* lifeSignCfg = ((const ELogConfigStringValue*)subValue)->getStringValue();
            if (!configureLifeSign(lifeSignCfg)) {
                return false;
            }
        }
    }
    return true;
}

bool configLifeSignProps(const ELogPropertySequence& props) {
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
    return true;
}

bool configLifeSign(const ELogConfigMapNode* cfgMap) {
    if (!configLifeSignBasic(cfgMap)) {
        return false;
    }

    std::string lifeSignLogFormat;
    bool found = false;
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
    return true;
}

}  // namespace elog

#endif