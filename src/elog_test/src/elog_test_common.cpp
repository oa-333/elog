#include "elog_test_common.h"

#ifdef ELOG_LINUX
#include <openssl/ssl.h>
#endif

#include <cstdio>
#include <cstdlib>

#ifndef ELOG_USING_DBG_UTIL
#ifdef ELOG_MINGW
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif  // ELOG_MINGW
#elif !defined(ELOG_WINDOWS)
#include <sys/syscall.h>
#ifdef SYS_gettid
#define gettid() syscall(SYS_gettid)
#else
#error "SYS_gettid unavailable on this system"
#endif  // SYS_gettid defined
#endif  // ELOG_USING_DBG_UTIL not defined

ELOG_IMPLEMENT_LOG_TARGET(TestLogTarget)

elog::ELogLogger* sTestLogger = nullptr;

static bool sDebugPrintEnabled = false;

bool isDebugPrintEnabled() { return sDebugPrintEnabled; }

#ifndef ELOG_USING_DBG_UTIL
uint32_t getCurrentThreadId() {
#ifdef ELOG_WINDOWS
    return ::GetCurrentThreadId();
#else
    return gettid();
#endif
}
#endif

void pinThread(uint32_t coreId) {
#ifdef ELOG_WINDOWS
    // SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)(1ull << coreId));
#else
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(coreId, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#endif
}

void tokenize(const char* str, std::vector<std::string>& tokens,
              const char* delims /* = " \t\r\n" */) {
    std::string s = str;
    std::string::size_type start = 0, end = 0;
    while ((start = s.find_first_not_of(delims, end)) != std::string::npos) {
        // start points to first non-delim char
        // now search for first delim char
        end = s.find_first_of(delims, start);
        tokens.push_back(s.substr(start, end - start));
    }
}

#ifdef ELOG_ENABLE_CONFIG_SERVICE
struct Publisher : public elog::ELogConfigServicePublisher {
    Publisher() : elog::ELogConfigServicePublisher("elog_test_publisher") {}
    bool load(const elog::ELogConfigMapNode* cfg) override { return true; }
    bool load(const elog::ELogPropertySequence& props) override { return true; }
    bool initialize() override { return true; }
    bool terminate() override { return true; }
    void onConfigServiceStart(const char* host, int port) override {
        ELOG_DEBUG_EX(sTestLogger, "ELog remote configuration service is ready at: %s:%d\n", host,
                      port);
    }
    void onConfigServiceStop(const char* host, int port) {
        ELOG_DEBUG_EX(sTestLogger, "ELog remote configuration service at %s:%d is down\n", host,
                      port);
    }
    bool publishConfigService() final { return true; }
    void unpublishConfigService() final {}
    void renewExpiry() final {}
    bool isConnected() final { return true; }
    bool connect() final { return true; }
};
Publisher publisher;
#endif

extern bool initTestEnv() {
    // #ifdef ELOG_LINUX
    // SSL_load_error_strings(); /* readable error messages */
    // SSL_library_init();       /* initialize library */
    // #endif
    std::string dbgPrintStr;
    getEnvVar("ELOG_TEST_DBG_PRINT", dbgPrintStr);
    if (dbgPrintStr.compare("TRUE") == 0) {
        sDebugPrintEnabled = true;
    }
    setlocale(LC_NUMERIC, "");
    ELOG_INFO("Accumulated message 1");
    ELOG_ERROR("Accumulated message 2");

    elog::ELogParams params;
#ifdef ELOG_ENABLE_CONFIG_SERVICE
    params.m_configServiceParams.m_configServiceHost = "localhost";
    params.m_configServiceParams.m_configServicePort = 6789;
    params.m_configServiceParams.m_publisher = &publisher;
#endif
    params.m_enableLogStatistics = true;
    if (!elog::initialize(params)) {
        fprintf(stderr, "Failed to initialize elog system\n");
        return false;
    }
    sTestLogger = elog::getSharedLogger("elog.test");
    elog::setCurrentThreadName("elog_test_main");
    return true;
}

extern void termTestEnv() { elog::terminate(); }

elog::ELogTarget* initElog(const char* cfg /* = DEFAULT_CFG */) {
    elog::setAppName("elog_test_app");
    // elog::addStdErrLogTarget();

    elog::ELogPropertyPosSequence props;
    std::string namedCfg = cfg;
    std::string::size_type nonSpacePos = namedCfg.find_first_not_of(" \t\r\n");
    if (nonSpacePos == std::string::npos) {
        ELOG_DEBUG_EX(sTestLogger, "Invalid log target configuration, all white space\n");
        return nullptr;
    }
    bool res = false;
    elog::ELogStatistics startStats;
    elog::getLogStatistics(startStats);
    if (namedCfg[nonSpacePos] != '{') {
        if (namedCfg.find("name=elog_test") == std::string::npos) {
            if (namedCfg.find('?') != std::string::npos) {
                namedCfg += "&name=elog_test";
            } else {
                namedCfg += "?name=elog_test";
            }
        }
        static int confType = 0;
        if (++confType % 2 == 0) {
            ELOG_DEBUG_EX(sTestLogger, "Using configuration: log_target = %s\n", namedCfg.c_str());
            elog::ELogStringPropertyPos* prop =
                new elog::ELogStringPropertyPos(namedCfg.c_str(), 0, 0);
            props.m_sequence.push_back({"log_target", prop});
            res = elog::configureByPropsEx(props, true, true);
        } else {
            std::string cfgStr = "{ log_target = \'";
            cfgStr += namedCfg + "\'}";
            ELOG_DEBUG_EX(sTestLogger, "Using configuration: log_target = %s\n", namedCfg.c_str());
            res = elog::configureByStr(cfgStr.c_str(), true, true);
        }
    } else {
        res = elog::configureByStr(cfg, true, true);
    }
    if (!res) {
        ELOG_DEBUG_EX(sTestLogger, "Failed to initialize elog system with log target config: %s\n",
                      cfg);
        return nullptr;
    }
    elog::ELogStatistics endStats;
    elog::getLogStatistics(endStats);
    if (!verifyNoErrors(startStats, endStats)) {
        ELOG_ERROR_EX(
            sTestLogger,
            "Encountered errors during initialization of elog system with log target config: %s\n",
            cfg);
        return nullptr;
    }
    ELOG_DEBUG_EX(sTestLogger, "Configure from props OK\n");

    elog::ELogTarget* logTarget = elog::getLogTarget("elog_test");
    if (logTarget == nullptr) {
        ELOG_DEBUG_EX(sTestLogger, "Failed to find logger by name elog_test, aborting\n");
        return nullptr;
    }
    elog::ELogSource* logSource = elog::defineLogSource("elog_test_logger");
    elog::ELogTargetAffinityMask mask = 0;
    ELOG_ADD_TARGET_AFFINITY_MASK(mask, logTarget->getId());
    logSource->setLogTargetAffinity(mask);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    return logTarget;
}

void termELog() { elog::clearAllLogTargets(); }

extern bool execProcess(const char* cmd, std::string& outputRes) {
    FILE* fp = NULL;
#ifdef ELOG_WINDOWS
    fp = _popen(cmd, "r");
#else
    fp = popen(cmd, "r");
#endif

    if (fp == NULL) {
        ELOG_SYS_ERROR(popen, "Failed to run command: %s", cmd);
        return false;
    }
    char buf[1024];
    while (fgets(buf, sizeof(buf), fp) != NULL) {
        outputRes += buf;
    }

#ifdef ELOG_WINDOWS
    _pclose(fp);
#else
    pclose(fp);
#endif
    return true;
}

void printPreInitMessages() {
    // this should trigger printing of pre-init messages
    elog::ELogTargetId id = elog::addStdErrLogTarget();
    elog::removeLogTarget(id);
    // elog::discardAccumulatedLogMessages();
}

#ifdef ELOG_MSVC
inline std::string win32FormatNumber(double number, unsigned precision = 3) {
    char fmtStr[32];
    snprintf(fmtStr, 32, "%%.%uf", precision);
    char numStr[32] = {};
    snprintf(numStr, 32, fmtStr, number);
    char buf[32] = {};
    NUMBERFMTA nf = {2, 0, 3, (LPSTR) ".", (LPSTR) ",", 1};
    GetNumberFormatA(LOCALE_NAME_USER_DEFAULT, 0, numStr, &nf, buf, 32);
    return buf;
}
#endif

void runSingleThreadedTest(const char* title, const char* cfg, double& msgThroughput,
                           double& ioThroughput, ThreadTestType testType /* = TT_NORMAL */,
                           uint32_t msgCount /* = ST_MSG_COUNT */, bool enableTrace /* = false */) {
    elog::ELogTarget* logTarget = initElog(cfg);
    if (logTarget == nullptr) {
        ELOG_DEBUG_EX(sTestLogger, "Failed to init %s test, aborting\n", title);
        return;
    }

    if (enableTrace) {
        elog::setReportLevel(elog::ELEVEL_TRACE);
    }

    ELOG_DEBUG_EX(sTestLogger, "\nRunning %s single-thread test\n", title);
    elog::ELogSource* logSource = elog::defineLogSource("elog.bench", true);
    elog::ELogLogger* logger = logSource->createPrivateLogger();

    elog::ELogCacheEntryId msgId = elog::getOrCacheFormatMsg("Single thread Test log {}");

    uint64_t bytesStart = logTarget->getBytesWritten();
    pinThread(0);
    auto start = std::chrono::high_resolution_clock::now();
    for (uint64_t i = 0; i < msgCount; ++i) {
        switch (testType) {
            case TT_NORMAL:
                ELOG_INFO_EX(logger, "Single thread Test log %u", i);
                break;

#ifdef ELOG_ENABLE_FMT_LIB
            case TT_BINARY:
                ELOG_BIN_INFO_EX(logger, "Single thread Test log {}", i);
                break;

            case TT_BINARY_CACHED:
                ELOG_CACHE_INFO_EX(logger, "Single thread Test log {}", i);
                break;

            case TT_BINARY_PRE_CACHED:
                ELOG_ID_INFO_EX(logger, msgId, i);
                break;
#endif
        }
    }
    auto end0 = std::chrono::high_resolution_clock::now();
    ELOG_DEBUG_EX(sTestLogger, "Finished logging, waiting for logger to catch up\n");
    logTarget->flush();
    while (!isCaughtUp(logTarget, msgCount)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(0));
    }

    auto end = std::chrono::high_resolution_clock::now();
    uint64_t bytesEnd = logTarget->getBytesWritten();
    std::chrono::microseconds testTime0 =
        std::chrono::duration_cast<std::chrono::microseconds>(end0 - start);
    std::chrono::microseconds testTime =
        std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    msgThroughput = msgCount / (double)testTime0.count() * 1000000.0f;
    ioThroughput = (bytesEnd - bytesStart) / (double)testTime.count() * 1000000.0f / 1024;
#ifdef ELOG_MSVC
    ELOG_DEBUG_EX(sTestLogger, "Throughput: %s MSg/Sec\n",
                  win32FormatNumber(msgThroughput).c_str());
    ELOG_DEBUG_EX(sTestLogger, "Throughput: %s KB/Sec\n\n",
                  win32FormatNumber(ioThroughput).c_str());
#else
    ELOG_DEBUG_EX(sTestLogger, "Throughput: %'.3f MSg/Sec\n", msgThroughput);
    ELOG_DEBUG_EX(sTestLogger, "Throughput: %'.3f KB/Sec\n\n", ioThroughput);
#endif

    termELog();
}

void runMultiThreadTest(const char* title, const char* fileName, const char* cfg,
                        ThreadTestType testType /* = TT_NORMAL */,
                        uint32_t msgCount /* = MT_MSG_COUNT */,
                        uint32_t minThreads /* = MIN_THREAD_COUNT */,
                        uint32_t maxThreads /* = MAX_THREAD_COUNT */,
                        bool privateLogger /* = true */, bool enableTrace /* = false */) {
    elog::ELogTarget* logTarget = initElog(cfg);
    if (logTarget == nullptr) {
        ELOG_DEBUG_EX(sTestLogger, "Failed to init %s test, aborting\n", title);
        return;
    }

    if (enableTrace) {
        elog::setReportLevel(elog::ELEVEL_TRACE);
    }

    ELOG_DEBUG_EX(sTestLogger, "\nRunning %s thread test [%u-%u]\n", title, minThreads, maxThreads);
    std::vector<double> msgThroughput;
    std::vector<double> byteThroughput;
    std::vector<double> accumThroughput;
    elog::ELogLogger* sharedLogger =
        privateLogger ? nullptr : elog::getSharedLogger("elog_test_logger");
    for (uint32_t i = MIN_THREAD_COUNT; i < minThreads; ++i) {
        msgThroughput.push_back(0);
        byteThroughput.push_back(0);
        accumThroughput.push_back(0);
    }
    for (uint32_t i = maxThreads + 1; i < MAX_THREAD_COUNT; ++i) {
        msgThroughput.push_back(0);
        byteThroughput.push_back(0);
        accumThroughput.push_back(0);
    }
    for (uint32_t threadCount = minThreads; threadCount <= maxThreads; ++threadCount) {
        // ELOG_DEBUG_EX(sTestLogger, "Running %u threads test\n", threadCount);
        ELOG_INFO("Running %u Thread Test", threadCount);
        std::vector<std::thread> threads;
        std::vector<double> resVec(threadCount, 0.0);
        auto start = std::chrono::high_resolution_clock::now();
        std::vector<elog::ELogLogger*> loggers(threadCount);
        // create private loggers before running threads, otherwise race condition may happen
        // (log source is not thread-safe)
        for (uint32_t i = 0; i < threadCount; ++i) {
            loggers[i] =
                sharedLogger != nullptr ? sharedLogger : elog::getPrivateLogger("elog_test_logger");
        }
        uint64_t bytesStart = logTarget->getBytesWritten();
        uint64_t initMsgCount = logTarget->getProcessedMsgCount();
        // ELOG_DEBUG_EX(sTestLogger, "Init msg count = %" PRIu64 "\n", initMsgCount);
        elog::ELogCacheEntryId msgId = elog::getOrCacheFormatMsg("Thread {} Test log {}");
        for (uint32_t i = 0; i < threadCount; ++i) {
            elog::ELogLogger* logger = loggers[i];
            threads.emplace_back(std::thread([i, &resVec, logger, msgCount, testType, msgId]() {
                std::string tname = std::string("worker-") + std::to_string(i);
                elog::setCurrentThreadName(tname.c_str());
                pinThread(i);
                auto start = std::chrono::high_resolution_clock::now();
                for (uint64_t j = 0; j < msgCount; ++j) {
                    switch (testType) {
                        case TT_NORMAL:
                            ELOG_INFO_EX(logger, "Thread Test log %u", i);
                            break;

#ifdef ELOG_ENABLE_FMT_LIB
                        case TT_BINARY:
                            ELOG_BIN_INFO_EX(logger, "Thread Test log {}", i);
                            break;

                        case TT_BINARY_CACHED:
                            ELOG_CACHE_INFO_EX(logger, "Thread Test log {}", i);
                            break;

                        case TT_BINARY_PRE_CACHED:
                            ELOG_ID_INFO_EX(logger, msgId, i);
                            break;
#endif
                    }
                }
                auto end = std::chrono::high_resolution_clock::now();
                std::chrono::microseconds testTime =
                    std::chrono::duration_cast<std::chrono::microseconds>(end - start);
                double throughput = msgCount / (double)testTime.count() * 1000000.0f;
                /*ELOG_DEBUG_EX(sTestLogger, "Test time: %u usec, msg count: %u\n",
                (unsigned)testTime.count(), (unsigned)msgCount); ELOG_DEBUG_EX(sTestLogger, ,
                "Throughput: %0.3f MSg/Sec\n", throughput);*/
                resVec[i] = throughput;
            }));
        }
        for (uint32_t i = 0; i < threadCount; ++i) {
            threads[i].join();
        }
        auto end0 = std::chrono::high_resolution_clock::now();
        ELOG_DEBUG_EX(sTestLogger, "Finished logging, waiting for logger to catch up\n");
        uint64_t targetMsgCount = initMsgCount + threadCount * msgCount;
        // ELOG_DEBUG_EX(sTestLogger, "Waiting for target msg count %" PRIu64 "\n",
        // targetMsgCount);
        logTarget->flush();  // required for net/ipc tests
        while (!isCaughtUp(logTarget, targetMsgCount)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(0));
        }

        auto end = std::chrono::high_resolution_clock::now();
        ELOG_INFO("%u Thread Test ended", threadCount);
        uint64_t bytesEnd = logTarget->getBytesWritten();
        double throughput = 0;
        for (double val : resVec) {
            throughput += val;
        }
#ifdef ELOG_MSVC
        ELOG_DEBUG_EX(sTestLogger, "%u thread accumulated throughput: %s Msg/Sec\n", threadCount,
                      win32FormatNumber(throughput, 2).c_str());
#else
        ELOG_DEBUG_EX(sTestLogger, "%u thread accumulated throughput: %'.2f Msg/Sec\n", threadCount,
                      throughput);
#endif
        accumThroughput.push_back(throughput);

        std::chrono::microseconds testTime0 =
            std::chrono::duration_cast<std::chrono::microseconds>(end0 - start);
        std::chrono::microseconds testTime =
            std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        throughput = threadCount * msgCount / (double)testTime0.count() * 1000000.0f;
        /*ELOG_DEBUG_EX(sTestLogger, "%u thread Test time: %u usec, msg count: %u\n", threadCount,
                (unsigned)testTime.count(), (unsigned)MSG_COUNT);*/
#ifdef ELOG_MSVC
        ELOG_DEBUG_EX(sTestLogger, "%u thread Throughput: %s MSg/Sec\n", threadCount,
                      win32FormatNumber(throughput).c_str());
#else
        ELOG_DEBUG_EX(sTestLogger, "%u thread Throughput: %'.3f MSg/Sec\n", threadCount,
                      throughput);
#endif
        msgThroughput.push_back(throughput);
        throughput = (bytesEnd - bytesStart) / (double)testTime.count() * 1000000.0f / 1024;
#ifdef ELOG_MSVC
        ELOG_DEBUG_EX(sTestLogger, "%u thread Throughput: %s KB/Sec\n\n", threadCount,
                      win32FormatNumber(throughput).c_str());
#else
        ELOG_DEBUG_EX(sTestLogger, "%u thread Throughput: %'.3f KB/Sec\n\n", threadCount,
                      throughput);
#endif
        byteThroughput.push_back(throughput);
    }

    // std::this_thread::sleep_for(std::chrono::milliseconds(5000));
    termELog();
}

bool verifyNoErrors(const elog::ELogStatistics& startStats, const elog::ELogStatistics& endStats) {
    if (endStats.m_msgCount[elog::ELEVEL_FATAL] > 0) {
        fprintf(stderr, "Encountered FATAL errors, declaring test failed\n");
        return false;
    }
    uint64_t errorCount =
        endStats.m_msgCount[elog::ELEVEL_ERROR] - startStats.m_msgCount[elog::ELEVEL_ERROR];
    if (errorCount > 0) {
        fprintf(stderr, "Encountered %u ERROR(s) errors, declaring test failed\n",
                (unsigned)errorCount);
        return false;
    }
    return true;
}