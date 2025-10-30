
#include <fstream>

#include "elog_test_common.h"

#ifdef ELOG_ENABLE_CONFIG_PUBLISH_REDIS
#include "cfg_srv/elog_config_service_redis_publisher.h"
#endif
#ifdef ELOG_ENABLE_CONFIG_PUBLISH_ETCD
#include "cfg_srv/elog_config_service_etcd_publisher.h"
#endif

static int testColors();
static int testException();
static int testEventLog();
static int testRegression();
static int testLifeSign();
static int testConfigService();
static int testSelector();
static int testFilter();
static int testFlushPolicy();
static int testLogFormatter();

static void testLogMacros() {
    // test once macro
    for (uint32_t i = 0; i < 10; ++i) {
        ELOG_ONCE_INFO("This is a test once message");
    }

    // test once thread macro
    for (uint32_t i = 0; i < 10; ++i) {
        ELOG_ONCE_THREAD_INFO("This is a test once thread message");
    }

    // test moderate macro
    for (uint32_t i = 0; i < 30; ++i) {
        ELOG_MODERATE_INFO(2, 1, elog::ELogTimeUnits::TU_SECONDS,
                           "This is a test moderate message (twice per second)");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // test every-N macro
    for (uint32_t i = 0; i < 30; ++i) {
        ELOG_EVERY_N_INFO(10, "This is a test every-N message (one in 10 messages, total 30)");
    }
}

static void testJson() {
    // test structured logging in JSON format

    // clang-format off
    const char* cfg = "sys://stderr?"
        "log_format={\n"
            "\t\"time\": ${time_epoch},\n"
            "\t\"level\": \"${level}\",\n"
            "\t\"thread_id\": ${tid},\n"
            "\t\"log_source\": \"${src}\",\n"
            "\t\"log_msg\": \"${msg}\"\n"
        "}";
    // clang-format on

    elog::ELogTarget* logTarget = initElog(cfg);
    if (logTarget == nullptr) {
        fprintf(stderr, "Failed to init async-thread-name test, aborting\n");
        return;
    }

    // test moderate macro
    for (uint32_t i = 0; i < 30; ++i) {
        ELOG_MODERATE_INFO(
            2, 1, elog::ELogTimeUnits::TU_SECONDS,
            "This is a test moderate message (twice per second) with JSON structured logging");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    termELog();
}

static void testReloadConfig() {
#ifdef ELOG_ENABLE_RELOAD_CONFIG
    const char* cfg =
        "sys://stderr?log_format=${time} ${level:6} [${tid:5}] [${tname}] ${src} ${msg}";

    elog::ELogTarget* logTarget = initElog(cfg);
    if (logTarget == nullptr) {
        fprintf(stderr, "Failed to init reload-config test, aborting\n");
        return;
    }

    // launch a fre threads with same log source, have them print a few times each second, then
    // after 3 seconds change log level
    elog::defineLogSource("test_source");

    fprintf(stderr, "Launching test threads\n");
    volatile bool done = false;
    std::vector<std::thread> threads;
    for (uint32_t i = 0; i < 5; ++i) {
        threads.emplace_back(std::thread([&done, i]() {
            std::string tname = std::string("test-thread-") + std::to_string(i);
            elog::setCurrentThreadName(tname.c_str());
            elog::ELogLogger* logger = elog::getPrivateLogger("test_source");
            while (!done) {
                ELOG_INFO_EX(logger, "Test message from thread %u", i);
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
        }));
    }

    // wait 1 second and set log level to WARN
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    fprintf(stderr, "Modifying log level to WARN by STRING (messages should stop)\n");
    elog::reloadConfigStr("{ test_source.log_level=WARN }");

    // wait 1 second and set log level back to INFO
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    fprintf(stderr, "Modifying log level back to INFO (messages should reappear)\n");
    elog::reloadConfigStr("{ test_source.log_level=INFO }");

    // wait 1 second and set log level to WARN (from file)
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    fprintf(stderr, "Modifying log level to WARN by FILE (messages should stop)\n");
    std::ofstream f("./test.cfg");
    f << "{ test_source.log_level=WARN }";
    f.close();
    elog::reloadConfigFile("./test.cfg");

    // wait 1 second and set log level back to INFO
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    fprintf(stderr, "Modifying log level back to INFO (messages should reappear)\n");
    elog::reloadConfigStr("{ test_source.log_level=INFO }");

    // wait 1 second and set log level to WARN (periodic update from file)
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    fprintf(stderr, "Modifying log level to WARN by PERIODIC update (messages should stop)\n");
    f.open("./test.cfg", std::ios::out | std::ios::trunc);
    f << "{ test_source.log_level=WARN }";
    f.close();
    elog::setPeriodicReloadConfigFile("./test.cfg");
    elog::setReloadConfigPeriodMillis(100);

    // wait 1 second and set log level back to INFO (by periodic update)
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    fprintf(stderr,
            "Modifying log level back to INFO by PERIODIC update (messages should reappear)\n");
    elog::reloadConfigStr("{ test_source.log_level=INFO }");
    f.open("./test.cfg", std::ios::out | std::ios::trunc);
    f << "{ test_source.log_level=INFO }";
    f.close();

    // NEGATIVE test
    // wait 1 second and stop periodic update
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    elog::setReloadConfigPeriodMillis(0);

    // now change lgo level in file and see there is no effect
    fprintf(stderr, "Modifying log level to WARN (no effect expected, messages should continue)\n");
    f.open("./test.cfg", std::ios::out | std::ios::trunc);
    f << "{ test_source.log_level=WARN }";
    f.close();

    // wait 1 second and set log level back to INFO
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    fprintf(stderr, "Modifying log level back to INFO (messages should reappear)\n");
    elog::reloadConfigStr("{ test_source.log_level=INFO }");

    fprintf(stderr, "Finishing test\n");
    done = true;
    for (uint32_t i = 0; i < 5; ++i) {
        threads[i].join();
    }
#endif
}

int main(int argc, char* argv[]) {
    testing::InitGoogleTest(&argc, argv);

    testing::AddGlobalTestEnvironment(new ELogEnvironment());
    return RUN_ALL_TESTS();
}

static int testAsyncThreadName() {
    const char* cfg =
        "async://quantum?quantum_buffer_size=2000000&name=elog_test | "
        "sys://stderr?log_format=${time} ${level:6} [${tid:5}] [${tname}] ${src} ${msg}";

    elog::ELogTarget* logTarget = initElog(cfg);
    if (logTarget == nullptr) {
        fprintf(stderr, "Failed to init async-thread-name test, aborting\n");
        return 1;
    }

    ELOG_INFO("Test thread name/id, expecting elog_test_main/%u", getCurrentThreadId());

    // wait for 1 message to be fully processed
    while (!isCaughtUp(logTarget, 1));

    std::thread t = std::thread([logTarget]() {
        elog::setCurrentThreadName("another_thread");
        ELOG_INFO("Test thread name/id, expecting another_thread/%u", getCurrentThreadId());

        uint64_t writeCount = 0;
        uint64_t readCount = 0;
        // wait for 2 messages to be fully processed
        while (!isCaughtUp(logTarget, 2));
    });

    t.join();

    termELog();
    return 0;
}

#ifdef ELOG_ENABLE_STACK_TRACE
static int testLogStackTrace() {
    const char* cfg =
        "async://quantum?quantum_buffer_size=1000&name=elog_test | "
        "sys://stderr?log_format=${time} ${level:6} [${tid:5}] [${tname}] ${src} ${msg}&"
        "flush_policy=immediate";

    elog::ELogTarget* logTarget = initElog(cfg);
    if (logTarget == nullptr) {
        fprintf(stderr, "Failed to init async-thread-name test, aborting\n");
        return 1;
    }

    ELOG_STACK_TRACE(elog::ELEVEL_INFO, "some test title 1", 0, "Testing stack trace for thread %u",
                     getCurrentThreadId());

    ELOG_APP_STACK_TRACE(elog::ELEVEL_INFO, "some test title 2", 0,
                         "Testing app stack trace for thread %u", getCurrentThreadId());

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    termELog();
    return 0;
}
#endif

static int testRegression() {
    int res = testAsyncThreadName();
    if (res != 0) {
        return res;
    }
#ifdef ELOG_ENABLE_STACK_TRACE
    res = testLogStackTrace();
    if (res != 0) {
        return res;
    }
#endif
    testLogMacros();
    testJson();
    testReloadConfig();
    return 0;
}

#ifdef ELOG_ENABLE_LIFE_SIGN
static int testAppLifeSign(uint32_t threadCount) {
    fprintf(stderr, "Application life-sign test starting\n");

    // test application level filter
    if (!elog::setLifeSignReport(
            elog::ELogLifeSignScope::LS_APP, elog::ELEVEL_INFO,
            elog::ELogFrequencySpec(elog::ELogFrequencySpecMethod::FS_EVERY_N_MESSAGES, 1))) {
        ELOG_ERROR("Failed to set life-sign report");
        return 1;
    }

    // launch threads
    std::vector<std::thread> threads;
    volatile bool done = false;
    fprintf(stderr, "Launching test threads\n");
    for (uint32_t i = 0; i < threadCount; ++i) {
        threads.emplace_back(std::thread([i, &done]() {
            std::string tname = std::string("test-thread-app-") + std::to_string(i);
            elog::setCurrentThreadName(tname.c_str());
            uint32_t count = 0;
            while (!done) {
                ELOG_INFO(
                    "This is a life sign log (count %u) from thread %u, with APP filter freq 1",
                    ++count, i);
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }));
        std::this_thread::sleep_for(std::chrono::milliseconds(77));
    }
    fprintf(stderr, "Launched all threads\n");

    // let threads work for 5 seconds and close
    std::this_thread::sleep_for(std::chrono::seconds(5));
    fprintf(stderr, "Wait ended, joining threads\n");
    done = true;
    for (auto& t : threads) {
        t.join();
    }
    fprintf(stderr, "All threads finished\n");

    if (!elog::removeLifeSignReport(elog::ELogLifeSignScope::LS_APP, elog::ELEVEL_INFO)) {
        ELOG_ERROR("Failed to remove life-sign report");
        return 1;
    }
    fprintf(stderr, "Application-level life-sign test finished\n");
    return 0;
}

static int testThreadLifeSign(uint32_t threadCount) {
    fprintf(stderr, "Thread-level life-sign test starting\n");

    std::vector<std::thread> threads;
    std::vector<int> threadRes(threadCount, 0);
    volatile bool done = false;
    for (uint32_t i = 0; i < threadCount; ++i) {
        threads.emplace_back(std::thread([i, &done, &threadRes]() {
            std::string tname = std::string("test-thread-") + std::to_string(i);
            elog::setCurrentThreadName(tname.c_str());
            if (!elog::setLifeSignReport(
                    elog::ELogLifeSignScope::LS_THREAD, elog::ELEVEL_INFO,
                    elog::ELogFrequencySpec(elog::ELogFrequencySpecMethod::FS_EVERY_N_MESSAGES,
                                            2))) {
                ELOG_ERROR("Failed to set life-sign report");
                threadRes[i] = 1;
            }
            uint32_t count = 0;
            while (!done) {
                ELOG_INFO(
                    "This is a life sign log (count %u) from thread %u, with THREAD filter "
                    "freq 2",
                    ++count, i);
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            threadRes[i] = 0;
        }));
        std::this_thread::sleep_for(std::chrono::milliseconds(77));
    }
    fprintf(stderr, "Launched all threads\n");

    // let threads work for 5 seconds and close
    std::this_thread::sleep_for(std::chrono::seconds(5));
    fprintf(stderr, "Wait ended, joining threads\n");
    done = true;
    for (auto& t : threads) {
        t.join();
    }
    for (int res : threadRes) {
        if (res != 0) {
            fprintf(stderr, "Thread-level filter test failed\n");
            return res;
        }
    }
    fprintf(stderr, "Thread-level life-sign test ended, aborting\n");
    return 0;
}

static int testLogSourceLifeSign(uint32_t threadCount) {
    fprintf(stderr, "log-source life-sign test starting\n");
    if (!elog::setLogSourceLifeSignReport(
            elog::ELEVEL_INFO,
            elog::ELogFrequencySpec(elog::ELogFrequencySpecMethod::FS_RATE_LIMIT, 5, 1,
                                    elog::ELogTimeUnits::TU_SECONDS),
            elog::getDefaultLogger()->getLogSource())) {
        ELOG_ERROR("Failed to set life-sign report for default logger");
        return 1;
    }

    std::vector<std::thread> threads;
    volatile bool done = false;
    for (uint32_t i = 0; i < 5; ++i) {
        threads.emplace_back(std::thread([i, &done]() {
            std::string tname = std::string("test-log-source-thread-") + std::to_string(i);
            elog::setCurrentThreadName(tname.c_str());
            uint32_t count = 0;
            while (!done) {
                ELOG_INFO(
                    "This is a life sign log (count %u) from thread %u, with LOG-SOURCE rate "
                    "limit "
                    "of 5 msg/sec",
                    ++count, i);
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }));
        std::this_thread::sleep_for(std::chrono::milliseconds(77));
    }
    fprintf(stderr, "Launched all threads\n");

    // let threads work for 5 seconds and close
    std::this_thread::sleep_for(std::chrono::seconds(5));
    fprintf(stderr, "Wait ended, joining threads\n");
    done = true;
    for (auto& t : threads) {
        t.join();
    }
    fprintf(stderr, "Log-source life-sign test ended\n");

    if (!elog::removeLogSourceLifeSignReport(elog::ELEVEL_INFO,
                                             elog::getDefaultLogger()->getLogSource())) {
        ELOG_ERROR("Failed to remove life-sign report for default logger");
        return 1;
    }
    return 0;
}

static int testTargetThreadLifeSign() {
    fprintf(stderr, "Target-thread life-sign test starting\n");
    bool threadReady = false;
    bool appReady = false;
    volatile bool done = false;
    std::mutex m;
    std::condition_variable cv;
    std::thread t = std::thread([&threadReady, &appReady, &m, &cv, &done]() {
        std::string tname = "test-life-sign-thread";
        elog::setCurrentThreadName(tname.c_str());

        {
            std::unique_lock<std::mutex> lock(m);
            threadReady = true;
            cv.notify_one();
            cv.wait(lock, [&appReady]() { return appReady; });
        }

        uint32_t count = 0;
        while (!done) {
            ELOG_INFO(
                "This is a life sign log (count %u) from test-life-sign-thread, with target "
                "thread "
                "rate limit of 3 msg/sec",
                ++count);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });

    // wait for test thread to finish wait
    {
        std::unique_lock<std::mutex> lock(m);
        cv.wait(lock, [&threadReady]() { return threadReady; });
    }

    // set life sign report for the target thread
    // NOTE: we must install a notifier on windows
    dbgutil::CVThreadNotifier notifier(cv);
    if (!elog::setThreadNotifier("test-life-sign-thread", &notifier)) {
        ELOG_ERROR("Failed to set target thread notifier");
        return 1;
    }

    if (!elog::setLifeSignReport(
            elog::ELogLifeSignScope::LS_THREAD, elog::ELEVEL_INFO,
            elog::ELogFrequencySpec(elog::ELogFrequencySpecMethod::FS_RATE_LIMIT, 3, 1,
                                    elog::ELogTimeUnits::TU_SECONDS),
            "test-life-sign-thread")) {
        ELOG_ERROR("Failed to set life-sign report for target thread 'test-life-sign-thread'");
        bool done = true;
        {
            std::unique_lock<std::mutex> lock(m);
            appReady = true;
            cv.notify_one();
        }
        return 1;
    }

    // notify thread it can start the test
    {
        std::unique_lock<std::mutex> lock(m);
        appReady = true;
        cv.notify_one();
    }
    fprintf(stderr, "Launched test thread\n");

    // let thread work for 5 seconds and close
    std::this_thread::sleep_for(std::chrono::seconds(5));
    fprintf(stderr, "Wait ended, joining thread\n");
    done = true;
    t.join();
    fprintf(stderr, "Target thread life-sign test ended\n");
    return 0;
}
#endif

static int testLifeSign() {
#ifdef ELOG_ENABLE_LIFE_SIGN
    // baseline test - no filter used, direct life sign report
    fprintf(stderr, "Running basic life-sign test\n");
    elog::ELogTarget* logTarget = initElog();
    if (logTarget == nullptr) {
        fprintf(stderr, "Failed to init life-sign test, aborting\n");
        return 1;
    }
    fprintf(stderr, "initElog() OK\n");

    // run simple test - write one record
    elog::reportLifeSign("Test life sign");
    std::this_thread::sleep_for(std::chrono::seconds(3));

    // app-scope test
    int res = testAppLifeSign(5);
    if (res != 0) {
        return res;
    }

    // current thread test
    res = testThreadLifeSign(5);
    if (res != 0) {
        return res;
    }

    // log source test
    res = testLogSourceLifeSign(5);
    if (res != 0) {
        return res;
    }

    // test target thread life-sign
    res = testTargetThreadLifeSign();
    if (res != 0) {
        return res;
    }

    abort();
    return 0;
#else
    return -1;
#endif
}

static int testConfigService() {
#ifdef ELOG_ENABLE_CONFIG_SERVICE
    // baseline test - no filter used, direct life sign report
    fprintf(stderr, "Running basic config-service test\n");
    elog::ELogTarget* logTarget = initElog();
    if (logTarget == nullptr) {
        fprintf(stderr, "Failed to init config-service test, aborting\n");
        return 1;
    }
    fprintf(stderr, "initElog() OK\n");

    elog::ELogConfigServicePublisher* publisher = nullptr;
#ifdef ELOG_ENABLE_CONFIG_PUBLISH_REDIS
    elog::ELogConfigServiceRedisPublisher* redisPublisher =
        elog::ELogConfigServiceRedisPublisher::create();
    std::string redisServerList;
    getEnvVar("ELOG_REDIS_SERVERS", redisServerList);
    redisPublisher->setServerList(redisServerList);
    publisher = redisPublisher;
#endif
#ifdef ELOG_ENABLE_CONFIG_PUBLISH_ETCD
    elog::ELogConfigServiceEtcdPublisher* etcdPublisher =
        elog::ELogConfigServiceEtcdPublisher::create();
    std::string etcdServerList;
    getEnvVar("ELOG_ETCD_SERVERS", etcdServerList);
    fprintf(stderr, "etcd server at: %s", etcdServerList.c_str());
    etcdPublisher->setServerList(etcdServerList);
    std::string etcdApiVersion;
    getEnvVar("ELOG_ETCD_API_VERSION", etcdApiVersion);
    if (!etcdApiVersion.empty()) {
        elog::ELogEtcdApiVersion apiVersion;
        if (!elog::convertEtcdApiVersion(etcdApiVersion.c_str(), apiVersion)) {
            return 2;
        }
        etcdPublisher->setApiVersion(apiVersion);
    }
    publisher = etcdPublisher;
#endif
    if (publisher != nullptr) {
        if (!publisher->initialize()) {
            fprintf(stderr, "Failed to initialize redis publisher\n");
            return 2;
        }
        if (!elog::stopConfigService()) {
            fprintf(stderr, "Failed to stop configuration service\n");
            publisher->terminate();
            return 2;
        }
        elog::setConfigServiceDetails("subnet:192.168.1.0", 0);
        elog::setConfigServicePublisher(publisher);
        if (!elog::startConfigService()) {
            fprintf(stderr, "Failed to restart configuration service\n");
            elog::setConfigServicePublisher(nullptr);
            publisher->terminate();
            return 2;
        }
    }

    // just print every second with two loggers
    elog::ELogLogger* logger1 = elog::getPrivateLogger("test.logger1");
    elog::ELogLogger* logger2 = elog::getPrivateLogger("test.logger2");
    logger1->getLogSource()->setLogLevel(elog::ELEVEL_INFO, elog::ELogPropagateMode::PM_NONE);
    logger2->getLogSource()->setLogLevel(elog::ELEVEL_TRACE, elog::ELogPropagateMode::PM_NONE);

    volatile bool stopTest = false;
    std::thread t1 = std::thread([logger1, &stopTest]() {
        while (!stopTest) {
            ELOG_INFO_EX(logger1, "test message from logger 1");
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    });

    std::thread t2 = std::thread([logger2, &stopTest]() {
        while (!stopTest) {
            ELOG_TRACE_EX(logger2, "test message from logger 2");
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    });

    printf("press any key to stop...");
    fflush(stdout);
    getchar();
    stopTest = true;
    t1.join();
    t2.join();

    termELog();
    // NOTE: we can remove publisher early, or let ELog destroy it during shutdown
    // we leave this comment  for future tests
    // must remove publisher before going out of scope
    /*if (publisher != nullptr) {
        if (!elog::setConfigServicePublisher(nullptr, true)) {
            fprintf(stderr, "Failed to remove publisher\n");
        }
        publisher->terminate();
        elog::destroyConfigServicePublisher(publisher);
    }*/
#endif
    return 0;
}

class TestSelector : public elog::ELogFieldSelector {
public:
    TestSelector(const elog::ELogFieldSpec& fieldSpec)
        : elog::ELogFieldSelector(elog::ELogFieldType::FT_TEXT, fieldSpec) {}
    TestSelector(const TestSelector&) = delete;
    TestSelector(TestSelector&&) = delete;
    TestSelector& operator=(const TestSelector&) = delete;

    void selectField(const elog::ELogRecord& record, elog::ELogFieldReceptor* receptor) final {
        std::string fieldStr = "test-field";
        receptor->receiveStringField(getTypeId(), fieldStr.c_str(), getFieldSpec(),
                                     fieldStr.length());
    }

private:
    ELOG_DECLARE_FIELD_SELECTOR(TestSelector, test, ELOG_NO_EXPORT)
};

ELOG_IMPLEMENT_FIELD_SELECTOR(TestSelector)

// TODO: define a string log target so we can examine the resulting text

int testSelector() {
    const char* cfg = "sys://stderr?log_format=${time} ${level:6} [${tid}] <${test}> ${src} ${msg}";
    elog::ELogTarget* logTarget = initElog(cfg);
    if (logTarget == nullptr) {
        return 1;
    }
    elog::ELogLogger* logger = elog::getPrivateLogger("elog_test_logger");
    ELOG_INFO_EX(logger, "This is a test message");
    termELog();
    return 0;
}

class TestFilter final : public elog::ELogFilter {
public:
    TestFilter() {}
    TestFilter(const TestFilter&) = delete;
    TestFilter(TestFilter&&) = delete;
    TestFilter& operator=(const TestFilter&) = delete;

    /** @brief Loads filter from configuration. */
    bool load(const elog::ELogConfigMapNode* filterCfg) final { return true; }

    /** @brief Loads filter from a free-style predicate-like parsed expression. */
    bool loadExpr(const elog::ELogExpression* expr) final { return true; }

    /**
     * @brief Filters a log record.
     * @param logRecord The log record to filter.
     * @return true If the log record is to be logged.
     * @return false If the log record is to be discarded.
     */
    bool filterLogRecord(const elog::ELogRecord& logRecord) final {
        return logRecord.m_logRecordId % 2 == 0;
    }

private:
    ELOG_DECLARE_FILTER(TestFilter, test_filter, ELOG_NO_EXPORT)
};

ELOG_IMPLEMENT_FILTER(TestFilter)

int testFilter() {
    const char* cfg =
        "sys://stderr?log_format=${time} ${level:6} [${tid}] <${test}> ${src} ${msg}&"
        "filter=test_filter";
    elog::ELogTarget* logTarget = initElog(cfg);
    if (logTarget == nullptr) {
        return 1;
    }
    elog::ELogLogger* logger = elog::getPrivateLogger("elog_test_logger");
    for (int i = 0; i < 10; ++i) {
        ELOG_INFO_EX(logger, "This is a test message %d", i);
    }
    termELog();
    return 0;
}

/**
 * @class A flush policy that enforces log target flush whenever the number of un-flushed log
 * messages exceeds a configured limit.
 */
class TestFlushPolicy final : public elog::ELogFlushPolicy {
public:
    TestFlushPolicy() : m_counter(0) {}
    TestFlushPolicy(const TestFlushPolicy&) = delete;
    TestFlushPolicy(TestFlushPolicy&&) = delete;
    TestFlushPolicy& operator=(const TestFlushPolicy&) = delete;

    /** @brief Loads flush policy from configuration. */
    bool load(const elog::ELogConfigMapNode* flushPolicyCfg) final { return true; }

    /** @brief Loads flush policy from a free-style predicate-like parsed expression. */
    bool loadExpr(const elog::ELogExpression* expr) final { return true; }

    bool shouldFlush(uint32_t msgSizeBytes) final {
        if ((++m_counter) % 2 == 0) {
            fprintf(stderr, "Test flush PASS\n");
            return true;
        } else {
            fprintf(stderr, "Test flush NO-PASS\n");
            return false;
        }
    }

private:
    uint64_t m_counter;

    ELOG_DECLARE_FLUSH_POLICY(TestFlushPolicy, test_policy, ELOG_NO_EXPORT)
};

ELOG_IMPLEMENT_FLUSH_POLICY(TestFlushPolicy)

int testFlushPolicy() {
    const char* cfg =
        "sys://stderr?log_format=${time} ${level:6} [${tid}] <${test}> ${src} ${msg}&"
        "flush_policy=test_policy";
    elog::ELogTarget* logTarget = initElog(cfg);
    if (logTarget == nullptr) {
        return 1;
    }
    elog::ELogLogger* logger = elog::getPrivateLogger("elog_test_logger");
    for (int i = 0; i < 10; ++i) {
        ELOG_INFO_EX(logger, "This is a test message %d", i);
    }
    termELog();
    return 0;
}

// test formatter - prepends the message with "***"", and surrounds each field with "[]"
class TestFormatter : public elog::ELogFormatter {
public:
    TestFormatter() : ELogFormatter(TYPE_NAME), m_firstField(true) {}
    TestFormatter(const TestFormatter&) = delete;
    TestFormatter(TestFormatter&&) = delete;
    TestFormatter& operator=(const TestFormatter&) = delete;

    static constexpr const char* TYPE_NAME = "test";

protected:
    bool handleText(const std::string& text) override {
        if (m_firstField) {
            m_fieldSelectors.push_back(new (std::nothrow) elog::ELogStaticTextSelector("***"));
            m_firstField = false;
        }
        m_fieldSelectors.push_back(new (std::nothrow) elog::ELogStaticTextSelector(text.c_str()));
        return true;
    }

    bool handleField(const elog::ELogFieldSpec& fieldSpec) override {
        m_fieldSelectors.push_back(new (std::nothrow) elog::ELogStaticTextSelector("["));
        bool res = ELogFormatter::handleField(fieldSpec);
        if (res == true) {
            m_fieldSelectors.push_back(new (std::nothrow) elog::ELogStaticTextSelector("]"));
        }
        return res;
    }

private:
    bool m_firstField;

    ELOG_DECLARE_LOG_FORMATTER(TestFormatter, test, ELOG_NO_EXPORT)
};

ELOG_IMPLEMENT_LOG_FORMATTER(TestFormatter)

int testLogFormatter() {
    const char* cfg = "sys://stderr?log_format=test:${time} ${level:6} ${tid} ${src} ${msg}";
    elog::ELogTarget* logTarget = initElog(cfg);
    if (logTarget == nullptr) {
        return 1;
    }
    elog::ELogLogger* logger = elog::getPrivateLogger("elog_test_logger");
    ELOG_INFO_EX(logger, "This is a test message");
    termELog();
    return 0;
}

void testPerfPrivateLog() {
    // Private logger test
    fprintf(stderr, "Running Empty Private logger test\n");
    elog::ELogTarget* logTarget = initElog();
    if (logTarget == nullptr) {
        fprintf(stderr, "Failed to init private logger test, aborting\n");
        return;
    }
    fprintf(stderr, "initElog() OK\n");
    elog::ELogLogger* privateLogger = elog::getPrivateLogger("");
    fprintf(stderr, "private logger retrieved\n");

    fprintf(stderr, "Empty private log benchmark:\n");
    uint64_t bytesStart = logTarget->getBytesWritten();
    auto start = std::chrono::high_resolution_clock::now();

    // run test
    for (uint64_t i = 0; i < ST_MSG_COUNT; ++i) {
        ELOG_DEBUG_EX(privateLogger, "Test log %u", i);
    }

    // no need to wait for test to end, because no messages were issued
    auto end = std::chrono::high_resolution_clock::now();
    uint64_t bytesEnd = logTarget->getBytesWritten();
    std::chrono::microseconds testTime =
        std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    // print test result
    fprintf(stderr, "Test time: %u usec\n", (unsigned)testTime.count());

    double throughput = ST_MSG_COUNT / (double)testTime.count() * 1000000.0f;
    fprintf(stderr, "Throughput: %0.3f MSg/Sec\n", throughput);

    throughput = (bytesEnd - bytesStart) / (double)testTime.count() * 1000000.0f / 1024;
    fprintf(stderr, "Throughput: %0.3f KB/Sec\n", throughput);

    termELog();
}

void testPerfSharedLogger() {
    // Shared logger test
    fprintf(stderr, "Running Empty Shared logger test\n");
    elog::ELogTarget* logTarget = initElog();
    if (logTarget == nullptr) {
        fprintf(stderr, "Failed to init shared logger test, aborting\n");
        return;
    }
    elog::ELogLogger* sharedLogger = elog::getSharedLogger("");

    fprintf(stderr, "Empty shared log benchmark:\n");
    uint64_t bytesStart = logTarget->getBytesWritten();
    auto start = std::chrono::high_resolution_clock::now();

    // run test
    for (uint64_t i = 0; i < ST_MSG_COUNT; ++i) {
        ELOG_DEBUG_EX(sharedLogger, "Test log %u", i);
    }

    // no need to wait for test to end, because no messages were issued
    auto end = std::chrono::high_resolution_clock::now();
    uint64_t bytesEnd = logTarget->getBytesWritten();
    std::chrono::microseconds testTime =
        std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    // print test result
    fprintf(stderr, "Test time: %u usec\n", (unsigned)testTime.count());

    double throughput = ST_MSG_COUNT / (double)testTime.count() * 1000000.0f;
    fprintf(stderr, "Throughput: %0.3f MSg/Sec\n", throughput);

    throughput = (bytesEnd - bytesStart) / (double)testTime.count() * 1000000.0f / 1024;
    fprintf(stderr, "Throughput: %0.3f KB/Sec\n", throughput);

    termELog();
}

int testColors() {
    const char* cfg =
        "sys://stderr?log_format=${time:font=faint} ${level:6:fg-color=green:bg-color=blue} "
        "[${tid:font=italic}] ${src:font=underline:fg-color=bright-red} "
        "${msg:font=cross-out,blink-rapid:fg-color=#993983}";
    elog::ELogTarget* logTarget = initElog(cfg);
    if (logTarget == nullptr) {
        return 1;
    }
    elog::ELogLogger* logger = elog::getPrivateLogger("elog_test_logger");
    ELOG_INFO_EX(logger, "This is a test message");
    termELog();

    cfg =
        "sys://stderr?log_format=${time:font=faint} "
        "${if: (log_level == INFO): ${fmt:begin-fg-color=green}: ${fmt:begin-fg-color=red}}"
        "${level:6}${fmt:default} "
        "[${tid:font=italic}] ${src:font=underline:fg-color=bright-red} "
        "${msg:font=cross-out,blink-rapid:fg-color=#993983}";
    logTarget = initElog(cfg);
    if (logTarget == nullptr) {
        return 2;
    }
    logger = elog::getPrivateLogger("elog_test_logger");
    ELOG_INFO_EX(logger, "This is a test message");
    ELOG_WARN_EX(logger, "This is a test message");
    termELog();

    cfg =
        "sys://stderr?log_format=${time:font=faint} "
        "${switch: ${level}:"
        "   ${case: ${const-level: INFO}: ${fmt:begin-fg-color=green}} :"
        "   ${case: ${const-level: WARN}: ${fmt:begin-fg-color=red}} :"
        "   ${case: ${const-level: ERROR}: ${fmt:begin-fg-color=magenta}} :"
        "   ${default: ${fmt:begin-fg-color=yellow}}}"
        "${level:6}${fmt:default} "
        "[${tid:font=italic}] ${src:font=underline:fg-color=bright-red} "
        "${msg:font=cross-out,blink-rapid:fg-color=#993983}";
    logTarget = initElog(cfg);
    if (logTarget == nullptr) {
        return 3;
    }
    logger = elog::getPrivateLogger("elog_test_logger");
    ELOG_INFO_EX(logger, "This is a test message");
    ELOG_WARN_EX(logger, "This is a test message");
    ELOG_ERROR_EX(logger, "This is a test message");
    ELOG_NOTICE_EX(logger, "This is a test message");
    termELog();

    cfg =
        "sys://stderr?log_format=${time:font=faint} "
        "${expr-switch: "
        "   ${case: (log_level == INFO): ${fmt:begin-fg-color=green}} :"
        "   ${case: (log_level == WARN): ${fmt:begin-fg-color=red}} :"
        "   ${case: (log_level == ERROR): ${fmt:begin-fg-color=magenta}} :"
        "   ${default: ${fmt:begin-fg-color=yellow}}}"
        "${level:6}${fmt:default} "
        "[${tid:font=italic}] ${src:font=underline:fg-color=bright-red} "
        "${msg:font=cross-out,blink-rapid:fg-color=#993983}";
    logTarget = initElog(cfg);
    logger = elog::getPrivateLogger("elog_test_logger");
    ELOG_INFO_EX(logger, "This is a test message");
    ELOG_WARN_EX(logger, "This is a test message");
    ELOG_ERROR_EX(logger, "This is a test message");
    ELOG_NOTICE_EX(logger, "This is a test message");
    termELog();
    return 0;
}

// Get an index value to the pEventTypeNames array based on
// the event type value.
#ifdef ELOG_WINDOWS
const char* pEventTypeNames[] = {"Error", "Warning", "Informational", "Audit Success",
                                 "Audit Failure"};
DWORD GetEventTypeName(DWORD EventType) {
    DWORD index = 0;

    switch (EventType) {
        case EVENTLOG_ERROR_TYPE:
            index = 0;
            break;
        case EVENTLOG_WARNING_TYPE:
            index = 1;
            break;
        case EVENTLOG_INFORMATION_TYPE:
            index = 2;
            break;
        case EVENTLOG_AUDIT_SUCCESS:
            index = 3;
            break;
        case EVENTLOG_AUDIT_FAILURE:
            index = 4;
            break;
    }

    return index;
}
#define MAX_TIMESTAMP_LEN 64
void GetTimestamp(const DWORD Time, char displayString[]) {
    ULONGLONG ullTimeStamp = 0;
    ULONGLONG SecsTo1970 = 116444736000000000;
    SYSTEMTIME st;
    FILETIME ft, ftLocal;

    ullTimeStamp = Int32x32To64(Time, 10000000) + SecsTo1970;
    ft.dwHighDateTime = (DWORD)((ullTimeStamp >> 32) & 0xFFFFFFFF);
    ft.dwLowDateTime = (DWORD)(ullTimeStamp & 0xFFFFFFFF);

    FileTimeToLocalFileTime(&ft, &ftLocal);
    FileTimeToSystemTime(&ftLocal, &st);
    snprintf(displayString, MAX_TIMESTAMP_LEN, "%u-%.2u-%.2u %.2u:%.2u:%.2u.%.3u", st.wYear,
             st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
}
#endif

int testEventLog() {
#ifdef ELOG_WINDOWS
    const char* cfg = "sys://eventlog?event_source_name=elog_test&event_id=1234&name=elog_test";
    double msgPerf = 0.0f;
    double ioPerf = 0.0f;
    time_t testStartTime = time(NULL);
    runSingleThreadedTest("Win32 Event Log", cfg, msgPerf, ioPerf, TT_NORMAL, 10);

    // now we need to find the events in the event log
    HANDLE hLog = OpenEventLogA(NULL, "elog_test");
    if (hLog == NULL) {
        ELOG_WIN32_ERROR(OpenEventLogA, "Could not open event log by name 'elog_test");
        return 1;
    }

    EVENTLOGRECORD buffer[4096];
    DWORD bytesRead, minBytesNeeded;
    if (!ReadEventLogA(hLog, EVENTLOG_SEQUENTIAL_READ | EVENTLOG_BACKWARDS_READ, 0, &buffer,
                       sizeof(buffer), &bytesRead, &minBytesNeeded)) {
        ELOG_WIN32_ERROR(ReadEventLogA, "Could not read event log by name 'elog_test");
        return 2;
    }

    // read recent events backwards and verify test result
    // we expect to see exactly 13 records (due to pre-init 2 log messages, and one test error
    // message at runSingleThreadedTest), which belong to elog_test provider and have a higher
    // timestamp, and we should stop when timestamp goes beyond test start time
    uint32_t matchingRecords = 0;
    PBYTE pRecord = (PBYTE)buffer;
    PBYTE pEndOfRecords = (PBYTE)(buffer + bytesRead);
    while (pRecord < pEndOfRecords) {
        PEVENTLOGRECORD eventRecord = (PEVENTLOGRECORD)pRecord;
        if (eventRecord->TimeGenerated < testStartTime) {
            break;
        }
        char* providerName = (char*)(pRecord + sizeof(EVENTLOGRECORD));
        uint32_t statusCode = eventRecord->EventID & 0xFFFF;
        if ((strcmp(providerName, "elog_test") == 0) && statusCode == 1234) {
            printf("provider name: %s\n", providerName);
            printf("status code: %d\n", statusCode);
            char timeStamp[MAX_TIMESTAMP_LEN];
            GetTimestamp(eventRecord->TimeGenerated, timeStamp);
            printf("Time stamp: %s\n", timeStamp);
            printf("record number: %lu\n", eventRecord->RecordNumber);
            printf("event type: %s\n", pEventTypeNames[GetEventTypeName(eventRecord->EventType)]);
            char* pMessage = (char*)(pRecord + eventRecord->StringOffset);
            if (pMessage != nullptr) {
                printf("event first string arg: %s\n", pMessage);
            }
            printf("\n");
            fflush(stdout);

            ++matchingRecords;
        }
        pRecord += eventRecord->Length;
    }

    CloseEventLog(hLog);
    if (matchingRecords != 13) {
        fprintf(stderr, "Event Log test failed, expecting 13 records, but instead found %u\n",
                matchingRecords);
        return 3;
    }
    return 0;
#else
    return -1;
#endif
}