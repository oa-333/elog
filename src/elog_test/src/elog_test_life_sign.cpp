#include "elog_test_common.h"

#ifdef ELOG_ENABLE_LIFE_SIGN
static int testAppLifeSign(uint32_t threadCount) {
    ELOG_DEBUG_EX(sTestLogger, "Application life-sign test starting");

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
    ELOG_DEBUG_EX(sTestLogger, "Launching test threads");
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
    ELOG_DEBUG_EX(sTestLogger, "Launched all threads");

    // let threads work for 5 seconds and close
    std::this_thread::sleep_for(std::chrono::seconds(5));
    ELOG_DEBUG_EX(sTestLogger, "Wait ended, joining threads");
    done = true;
    for (auto& t : threads) {
        t.join();
    }
    ELOG_DEBUG_EX(sTestLogger, "All threads finished");

#if 0
    // check we get meaningful output
    std::string cmd = "elog_pm --ls-shm";
    std::string outputRes;
    if (!execProcess(cmd.c_str(), outputRes)) {
        ELOG_ERROR_EX(sTestLogger, "Failed to execute sub-process: %s", cmd.c_str());
        return 2;
    }

    // parse lines
    std::vector<std::string> shmList;
    tokenize(outputRes.c_str(), shmList);
    if (shmList.size() <= 2) {
        ELOG_ERROR_EX(sTestLogger, "Missing shared memory segment");
        return 3;
    }

    // find the segment for this process
    // format is: dbgutil.life-sign.elog_test.exe.2025-08-20_10-55-46.12312.shm
    std::string shmSegment;
    std::string prefix = "dbgutil.life-sign.elog_test.exe.";
    std::string date = "xxxx-xx-xx_xx_xx.";
    std::string suffix = ".shm";
    for (const std::string& shmName : shmList) {
        std::vector<std::string> shmTokens;
        tokenize(shmName.c_str(), shmTokens, ".");
        if (shmTokens.size() != 7) {
            continue;
        }
        if (shmTokens[0].compare("dbgutil") == 0 && shmTokens[1].compare("life-sign") == 0 &&
            shmTokens[2].compare("elog_test") == 0 && shmTokens[3].compare("exe") == 0 &&
            shmTokens[6].compare("shm") == 0) {
            int processId = 0;
            try {
                processId = std::stoi(shmTokens[5]);
            } catch (std::exception&) {
            }
            if (processId == getpid()) {
                // segment found
                shmSegment = shmName;
                break;
            }
        }
        if (shmName.starts_with(prefix) && shmName.size() >= prefix.size() + date.size()) {
            std::string suffix = shmName.substr(prefix.size() + date.size());
        }
    }

    // now list segment and verify contents are as expected...
#endif

    if (!elog::removeLifeSignReport(elog::ELogLifeSignScope::LS_APP, elog::ELEVEL_INFO)) {
        ELOG_ERROR("Failed to remove life-sign report");
        return 1;
    }
    ELOG_DEBUG_EX(sTestLogger, "Application-level life-sign test finished");
    return 0;
}

static int testThreadLifeSign(uint32_t threadCount) {
    ELOG_DEBUG_EX(sTestLogger, "Thread-level life-sign test starting");

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
    ELOG_DEBUG_EX(sTestLogger, "Launched all threads");

    // let threads work for 5 seconds and close
    std::this_thread::sleep_for(std::chrono::seconds(5));
    ELOG_DEBUG_EX(sTestLogger, "Wait ended, joining threads");
    done = true;
    for (auto& t : threads) {
        t.join();
    }
    for (int res : threadRes) {
        if (res != 0) {
            ELOG_ERROR("Thread-level filter test failed");
            return res;
        }
    }
    ELOG_DEBUG_EX(sTestLogger, "Thread-level life-sign test ended, aborting");
    return 0;
}

static int testLogSourceLifeSign(uint32_t threadCount) {
    ELOG_DEBUG_EX(sTestLogger, "log-source life-sign test starting");
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
    ELOG_DEBUG_EX(sTestLogger, "Launched all threads");

    // let threads work for 5 seconds and close
    std::this_thread::sleep_for(std::chrono::seconds(5));
    ELOG_DEBUG_EX(sTestLogger, "Wait ended, joining threads");
    done = true;
    for (auto& t : threads) {
        t.join();
    }
    ELOG_DEBUG_EX(sTestLogger, "Log-source life-sign test ended");

    if (!elog::removeLogSourceLifeSignReport(elog::ELEVEL_INFO,
                                             elog::getDefaultLogger()->getLogSource())) {
        ELOG_ERROR("Failed to remove life-sign report for default logger");
        return 1;
    }
    return 0;
}

static int testTargetThreadLifeSign() {
    ELOG_DEBUG_EX(sTestLogger, "Target-thread life-sign test starting");
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
    ELOG_DEBUG_EX(sTestLogger, "Launched test thread");

    // let thread work for 5 seconds and close
    std::this_thread::sleep_for(std::chrono::seconds(5));
    ELOG_DEBUG_EX(sTestLogger, "Wait ended, joining thread");
    done = true;
    t.join();
    ELOG_DEBUG_EX(sTestLogger, "Target thread life-sign test ended");
    return 0;
}

static int testLifeSign() {
    // baseline test - no filter used, direct life sign report
    ELOG_DEBUG_EX(sTestLogger, "Running basic life-sign test");
    elog::ELogTarget* logTarget = initElog();
    if (logTarget == nullptr) {
        ELOG_ERROR("Failed to init life-sign test, aborting");
        return 1;
    }
    ELOG_DEBUG_EX(sTestLogger, "initElog() OK");

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

    // TODO: required death test
    // abort();
    return 0;
}

TEST(ELogCore, LifeSign) {
    int res = testLifeSign();
    EXPECT_EQ(res, 0);
}
#endif