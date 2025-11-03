#include <fstream>

#include "elog_test_common.h"

#ifdef ELOG_ENABLE_RELOAD_CONFIG

static bool verifyMessagesStopped(TestLogTarget* logTarget, uint32_t checkFreqMillis = 100,
                                  uint32_t checkTimeoutMillis = 1000,
                                  uint32_t stabilityTimeoutMillis = 500) {
    size_t msgCount = 0;
    {
        std::unique_lock lock(logTarget->getLock());
        msgCount = logTarget->getLogMessages().size();
    }

    // sleep for 200 millis in loop, until we see
    uint32_t freezeIterations = 0;
    uint32_t checkIterations = checkTimeoutMillis / checkFreqMillis + 1;
    uint32_t stabilityIterations = stabilityTimeoutMillis / checkFreqMillis + 1;
    for (uint32_t i = 0; i < checkIterations; ++i) {
        std::unique_lock lock(logTarget->getLock());
        size_t newMsgCount = logTarget->getLogMessages().size();
        if (newMsgCount > msgCount) {
            msgCount = newMsgCount;
            freezeIterations = 0;
        } else {
            ++freezeIterations;
            if (freezeIterations == stabilityIterations) {
                // result is stable enough
                return true;
            }
        }
    }

    return false;
}

static bool verifyMessagesContinue(TestLogTarget* logTarget, uint32_t checkFreqMillis = 100,
                                   uint32_t checkTimeoutMillis = 1000,
                                   uint32_t stabilityTimeoutMillis = 500) {
    return !verifyMessagesStopped(logTarget, checkFreqMillis, checkTimeoutMillis,
                                  stabilityTimeoutMillis);
}

TEST(ELogCore, ReloadConfig) {
    TestLogTarget* logTarget = new (std::nothrow) TestLogTarget();
    logTarget->setLogFormat("${msg}");
    elog::addLogTarget(logTarget);

    const auto& logMessages = logTarget->getLogMessages();

    // launch a fre threads with same log source, have them print a few times each second, then
    // after 3 seconds change log level
    elog::defineLogSource("test_source");

    ELOG_DEBUG_EX(sTestLogger, "Launching test threads\n");
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
    ELOG_DEBUG_EX(sTestLogger, "Modifying log level to WARN by STRING (messages should stop)\n");
    elog::reloadConfigStr("{ test_source.log_level=WARN }");

    // verify message stopped
    bool res = verifyMessagesStopped(logTarget);

    // wait 1 second and set log level back to INFO
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    ELOG_DEBUG_EX(sTestLogger, "Modifying log level back to INFO (messages should reappear)\n");
    elog::reloadConfigStr("{ test_source.log_level=INFO }");

    // verify message continue
    res = verifyMessagesContinue(logTarget);

    // wait 1 second and set log level to WARN (from file)
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    ELOG_DEBUG_EX(sTestLogger, "Modifying log level to WARN by FILE (messages should stop)\n");
    std::ofstream f("./test.cfg");
    f << "{ test_source.log_level=WARN }";
    f.close();
    elog::reloadConfigFile("./test.cfg");

    // verify message stopped
    res = verifyMessagesStopped(logTarget);

    // wait 1 second and set log level back to INFO
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    ELOG_DEBUG_EX(sTestLogger, "Modifying log level back to INFO (messages should reappear)\n");
    elog::reloadConfigStr("{ test_source.log_level=INFO }");

    // verify message continue
    res = verifyMessagesContinue(logTarget);

    // wait 1 second and set log level to WARN (periodic update from file)
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    ELOG_DEBUG_EX(sTestLogger,
                  "Modifying log level to WARN by PERIODIC update (messages should stop)\n");
    f.open("./test.cfg", std::ios::out | std::ios::trunc);
    f << "{ test_source.log_level=WARN }";
    f.close();
    elog::setPeriodicReloadConfigFile("./test.cfg");
    elog::setReloadConfigPeriodMillis(100);

    // verify message stopped
    res = verifyMessagesStopped(logTarget);

    // wait 1 second and set log level back to INFO (by periodic update)
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    ELOG_TRACE_EX(
        sTestLogger,
        "Modifying log level back to INFO by PERIODIC update (messages should reappear)\n");
    elog::reloadConfigStr("{ test_source.log_level=INFO }");
    f.open("./test.cfg", std::ios::out | std::ios::trunc);
    f << "{ test_source.log_level=INFO }";
    f.close();

    // verify message continue
    res = verifyMessagesContinue(logTarget);

    // NEGATIVE test
    // wait 1 second and stop periodic update
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    elog::setReloadConfigPeriodMillis(0);

    // now change lgo level in file and see there is no effect
    ELOG_DEBUG_EX(sTestLogger,
                  "Modifying log level to WARN (no effect expected, messages should continue)\n");
    f.open("./test.cfg", std::ios::out | std::ios::trunc);
    f << "{ test_source.log_level=WARN }";
    f.close();

    // wait 1 second and set log level back to INFO
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    ELOG_DEBUG_EX(sTestLogger,
                  "Modifying log level back to INFO (messages should still be visible)\n");
    elog::reloadConfigStr("{ test_source.log_level=INFO }");

    // verify message continue
    res = verifyMessagesContinue(logTarget);

    ELOG_DEBUG_EX(sTestLogger, "Finishing test\n");
    done = true;
    for (uint32_t i = 0; i < 5; ++i) {
        threads[i].join();
    }
}
#endif