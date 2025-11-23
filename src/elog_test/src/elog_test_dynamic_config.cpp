

#include "elog_test_common.h"

#ifdef ELOG_ENABLE_DYNAMIC_CONFIG

// TODO: all tests must be repeated with a tight loop (no sleep, so we might experience real race
// conditions and even trigger a crash)

TEST(ELogDynamicConfig, TargetConfigRemove) {
    TestLogTarget* logTarget = new (std::nothrow) TestLogTarget();
    ASSERT_NE(logTarget, nullptr);

    bool res = logTarget->setLogFormat("${msg}");
    ASSERT_EQ(res, true);

    elog::ELogTargetId id = elog::addLogTarget(logTarget);
    ASSERT_NE(id, ELOG_INVALID_TARGET_ID);

    std::thread removeThread = std::thread([id, &res]() {
        // sleep a bit then remove
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        res = elog::removeLogTarget(id);
    });

    // in the meantime we repeatedly try to get the log target until we receive null or timeout
    // first should not be null since remove thread should be still sleeping
    elog::ELogTarget* target = elog::getLogTarget(id);
    ASSERT_NE(target, nullptr);
    for (int i = 0; i < 10; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        target = elog::getLogTarget(id);
        if (target == nullptr) {
            break;
        }
    }
    ASSERT_EQ(target, nullptr);

    removeThread.join();
    ASSERT_EQ(res, true);
    elog::clearAllLogTargets();
}

TEST(ELogDynamicConfig, TargetConfigRemoveMany) {
    TestLogTarget* logTarget = new (std::nothrow) TestLogTarget();
    ASSERT_NE(logTarget, nullptr);

    bool res = logTarget->setLogFormat("${msg}");
    ASSERT_EQ(res, true);

    elog::ELogTargetId id = elog::addLogTarget(logTarget);
    ASSERT_NE(id, ELOG_INVALID_TARGET_ID);

    // order many threads to remove, only one should succeed
    std::atomic<uint64_t> removeCount = 0;
    std::vector<std::thread> removeThreads;
    for (uint32_t i = 0; i < 32; ++i) {
        removeThreads.emplace_back(std::thread([id, &removeCount]() {
            // sleep a bit then remove
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            bool res = elog::removeLogTarget(id);
            if (res) {
                removeCount.fetch_add(1, std::memory_order_relaxed);
            }
        }));
    }

    // join all threads
    for (auto& t : removeThreads) {
        t.join();
    }

    ASSERT_EQ(removeCount.load(std::memory_order_seq_cst), 1);

    elog::clearAllLogTargets();
}

TEST(ELogDynamicConfig, TargetConfigLogRemove) {
    TestLogTarget* logTarget = new (std::nothrow) TestLogTarget();
    ASSERT_NE(logTarget, nullptr);

    bool res = logTarget->setLogFormat("${msg}");
    ASSERT_EQ(res, true);

    elog::ELogTargetId id = elog::addLogTarget(logTarget);
    ASSERT_NE(id, ELOG_INVALID_TARGET_ID);

    // attach a logger to the target
    elog::ELogLogger* logger = elog::getSharedLogger("elog.test_log_source");
    ASSERT_NE(logger, nullptr);
    logger->getLogSource()->pairWithLogTarget(logTarget);

    // now run background thread to remove the log target at some point
    std::thread removeThread = std::thread([id, &res]() {
        // sleep a bit then remove
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        res = elog::removeLogTarget(id);
    });

    // in the meantime we repeatedly log a message
    elog::ELogTarget* target = elog::getLogTarget(id);
    ASSERT_NE(target, nullptr);
    logTarget->clearLogMessages();
    uint64_t expectedMsgCount = 0;
    for (uint32_t i = 0; i < 20; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        // NOTE: we should not crash even after the log target has been removed
        ELOG_INFO_EX(logger, "Test message %u", i);
        if (elog::getLogTarget(id) != nullptr) {
            ++expectedMsgCount;
        }
    }
    uint64_t msgCount = logTarget->getLogMessages().size();

    // join the remove thread
    removeThread.join();
    ASSERT_EQ(res, true);

    // expected message count is about 10, assume deviation is 2 messages (i.e. 10 milliseconds)
    // due to timing issues we may need a better way to check this
    fprintf(stderr, "Message count: %u\n", (unsigned)msgCount);
    // expected message count is lower bound, and actual message count may differ by at most 1
    ASSERT_GE(msgCount, expectedMsgCount);
    ASSERT_LE(msgCount - expectedMsgCount, 1);

    elog::clearAllLogTargets();
}

TEST(ELogDynamicConfig, TargetConfigAndRemove) {
    TestLogTarget* logTarget = new (std::nothrow) TestLogTarget();
    ASSERT_NE(logTarget, nullptr);

    bool res = logTarget->setLogFormat("${msg}");
    ASSERT_EQ(res, true);

    elog::ELogTargetId id = elog::addLogTarget(logTarget);
    ASSERT_NE(id, ELOG_INVALID_TARGET_ID);

    // attach a logger to the target
    elog::ELogLogger* logger = elog::getSharedLogger("elog.test_log_source");
    ASSERT_NE(logger, nullptr);
    logger->getLogSource()->pairWithLogTarget(logTarget);

    // now run background thread to remove the log target at some point
    std::thread removeThread = std::thread([id, &res]() {
        // sleep a bit then remove
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        res = elog::removeLogTarget(id);
    });

    // in the meantime we acquire the log target, sleep, configure it and then repeatedly log a
    // message, then finally release it
    uint64_t epoch = 0;
    elog::ELogTarget* target = elog::acquireLogTarget(id, epoch);
    ASSERT_NE(target, nullptr);
    elog::enableLogStatistics();
    elog::ELogStatistics startStats;
    elog::getLogStatistics(startStats);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    target->setLogFormat("XXX ${msg}");

    // prepare stub log record
    elog::ELogRecord logRecord = {};
    elog::elogGetCurrentTime(logRecord.m_logTime);
    logRecord.m_logRecordId = 0;
    logRecord.m_logLevel = elog::ELEVEL_INFO;
    logRecord.m_threadId = 0;
    logRecord.m_logger = logger;
    logRecord.m_file = __FILE__;
    logRecord.m_function = ELOG_FUNCTION;
    logRecord.m_line = __LINE__;
    logRecord.m_logMsg = "Test message";

    uint32_t initMsgCount = logTarget->getLogMessages().size();
    for (uint32_t i = 0; i < 20; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        ASSERT_EQ(elog::getLogTarget(id), nullptr);
        // NOTE: we should not crash even after the log target has been removed
        ELOG_INFO_EX(logger, "Test message");

        // we should be able to log without crashing
        target->log(logRecord);
    }
    elog::ELogStatistics endStats;
    elog::getLogStatistics(endStats);
    uint64_t msgCount =
        endStats.m_msgCount[elog::ELEVEL_INFO] - startStats.m_msgCount[elog::ELEVEL_INFO];
    // NOTE: not even one message should be counted in global statistics, since the target was
    // already removed
    ELOG_INFO_EX(sTestLogger, "Message count: %u", (unsigned)msgCount);
    ASSERT_EQ(msgCount, 0);

    // verify log message count and format
    uint32_t endMsgCount = logTarget->getLogMessages().size();
    ASSERT_EQ(endMsgCount - initMsgCount, 20);
    for (uint32_t i = 0; i < logTarget->getLogMessages().size(); ++i) {
        if (i >= initMsgCount) {
            const std::string& msg = logTarget->getLogMessages()[i];
            ASSERT_EQ(msg.compare("XXX Test message"), 0);
        }
    }

    // now release log target
    elog::releaseLogTarget(epoch);

    // now verify target has been released, we need to sleep a bit though
    std::this_thread::sleep_for(std::chrono::seconds(1));
    ASSERT_EQ(elog::getLogTarget(id), nullptr);

    // join the remove thread
    removeThread.join();
    ASSERT_EQ(res, true);

    // verify that log target pointer is dead-land?

    elog::clearAllLogTargets();
}

TEST(ELogDynamicConfig, TargetConfigAndAdd) {
    // test plan: add a log target while another thread logs messages
    // we first add one log target, then start logging messages in the background, and then add a
    // second log target, and verify that the second log target also receives messages and there is
    // no crash
    TestLogTarget* logTarget = new (std::nothrow) TestLogTarget();
    ASSERT_NE(logTarget, nullptr);

    bool res = logTarget->setLogFormat("${msg}");
    ASSERT_EQ(res, true);

    elog::ELogTargetId id = elog::addLogTarget(logTarget);
    ASSERT_NE(id, ELOG_INVALID_TARGET_ID);
    logTarget->clearLogMessages();

    // now run background thread to remove the log target at some point
    std::thread logThread = std::thread([]() {
        for (uint32_t i = 0; i < 100; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            ELOG_INFO("Test message");
        }
    });

    // in the meantime we add another log target
    TestLogTarget* logTarget2 = new (std::nothrow) TestLogTarget();
    ASSERT_NE(logTarget2, nullptr);

    res = logTarget2->setLogFormat("${msg}");
    ASSERT_EQ(res, true);

    // sleep a bit so it happens concurrently with logging thread
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    elog::ELogTargetId id2 = elog::addLogTarget(logTarget2);
    ASSERT_NE(id2, ELOG_INVALID_TARGET_ID);
    // logTarget2->clearLogMessages();

    // now join the log thread
    logThread.join();

    // verify both log targets have messages, the first more than the second, but both have almost
    // 100 messages
    // first log target should have exactly 100 info log messages
    // second log target should have less
    ASSERT_EQ(logTarget->getInfoLogMessages().size(), 100);
    ASSERT_LT(logTarget2->getInfoLogMessages().size(), 100);
    ASSERT_GT(logTarget2->getInfoLogMessages().size(), 0);

    elog::clearAllLogTargets();
}

TEST(ELogDynamicConfig, TargetConfigReplaceFormat) {
    TestLogTarget* logTarget = new (std::nothrow) TestLogTarget();
    ASSERT_NE(logTarget, nullptr);

    bool res = logTarget->setLogFormat("${msg}");
    ASSERT_EQ(res, true);

    elog::ELogTargetId id = elog::addLogTarget(logTarget);
    ASSERT_NE(id, ELOG_INVALID_TARGET_ID);
    logTarget->clearLogMessages();

    // now run background thread to remove the log target at some point
    std::thread logThread = std::thread([]() {
        for (uint32_t i = 0; i < 100; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            ELOG_INFO("Test message");
        }
    });

    // sleep a bit and replace the log format
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    logTarget->setLogFormat("XXX ${msg}");

    // now join the log thread
    logThread.join();

    // verify log target has 100 messages, later part of it has new format
    ASSERT_EQ(logTarget->getInfoLogMessages().size(), 100);
    bool formatChanged = false;
    for (uint32_t i = 0; i < 100; ++i) {
        if (logTarget->getInfoLogMessages()[i].compare("Test message") == 0) {
            ASSERT_EQ(formatChanged, false);
        } else {
            if (!formatChanged) {
                formatChanged = true;
            }
            ASSERT_EQ(logTarget->getInfoLogMessages()[i].compare("XXX Test message"), 0);
        }
    }
    ASSERT_EQ(formatChanged, true);

    elog::clearAllLogTargets();
}

TEST(ELogDynamicConfig, TargetConfigReplaceFilter) {
    TestLogTarget* logTarget = new (std::nothrow) TestLogTarget();
    ASSERT_NE(logTarget, nullptr);

    bool res = logTarget->setLogFormat("${msg}");
    ASSERT_EQ(res, true);

    elog::ELogTargetId id = elog::addLogTarget(logTarget);
    ASSERT_NE(id, ELOG_INVALID_TARGET_ID);
    logTarget->clearLogMessages();

    // now run background thread to remove the log target at some point
    std::thread logThread = std::thread([]() {
        for (uint32_t i = 0; i < 100; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            ELOG_INFO("%u", i);
        }
    });

    // sleep a bit and replace the log filter
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    logTarget->setLogFilter(new (std::nothrow) elog::ELogCountFilter(2));

    // now join the log thread
    logThread.join();

    // verify log target has at most 100 messages, later part of it contains every second message
    // FIX THIS: at first deviation it begins jumps of 2, could be odd or even
    ASSERT_LE(logTarget->getInfoLogMessages().size(), 100);
    bool formatChanged = false;
    uint32_t prevJ = 0;
    for (uint32_t i = 0; i < logTarget->getInfoLogMessages().size(); ++i) {
        uint32_t j = std::stoul(logTarget->getInfoLogMessages()[i]);
        if (j == i) {
            ASSERT_EQ(formatChanged, false);
        } else {
            if (!formatChanged) {
                formatChanged = true;
            } else {
                ASSERT_EQ(j, prevJ + 2);
            }
            prevJ = j;
        }
    }
    ASSERT_EQ(formatChanged, true);

    elog::clearAllLogTargets();
}

TEST(ELogDynamicConfig, TargetConfigReplaceFlushPolicy) {
    TestLogTarget* logTarget = new (std::nothrow) TestLogTarget();
    ASSERT_NE(logTarget, nullptr);

    bool res = logTarget->setLogFormat("${msg}");
    ASSERT_EQ(res, true);

    elog::ELogTargetId id = elog::addLogTarget(logTarget);
    ASSERT_NE(id, ELOG_INVALID_TARGET_ID);
    logTarget->clearLogMessages();

    // attach a logger to the target
    elog::ELogLogger* logger = elog::getSharedLogger("elog.test_log_source");
    ASSERT_NE(logger, nullptr);
    logger->getLogSource()->pairWithLogTarget(logTarget);

    // now run background thread to remove the log target at some point
    std::thread logThread = std::thread([logger]() {
        for (uint32_t i = 0; i < 100; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            ELOG_INFO_EX(logger, "Test message");
        }
    });

    // sleep a bit and replace the log flush policy
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    logTarget->setFlushPolicy(new (std::nothrow) elog::ELogCountFlushPolicy(2));

    // now join the log thread
    logThread.join();

    // verify log target has exactly 100 messages
    ASSERT_EQ(logTarget->getInfoLogMessages().size(), 100);
    for (uint32_t i = 0; i < 100; ++i) {
        ASSERT_EQ(logTarget->getInfoLogMessages()[i].compare("Test message"), 0);
    }

    elog::clearAllLogTargets();
}

TEST(ELogDynamicConfig, GlobalConfigReplaceFormat) {
    TestLogTarget* logTarget = new (std::nothrow) TestLogTarget();
    ASSERT_NE(logTarget, nullptr);

    bool res = elog::configureLogFormat("${msg}");
    ASSERT_EQ(res, true);

    elog::ELogTargetId id = elog::addLogTarget(logTarget);
    ASSERT_NE(id, ELOG_INVALID_TARGET_ID);
    logTarget->clearLogMessages();

    // now run background thread to remove the log target at some point
    std::thread logThread = std::thread([]() {
        for (uint32_t i = 0; i < 100; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            ELOG_INFO("Test message");
        }
    });

    // sleep a bit and replace the log format
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    elog::configureLogFormat("XXX ${msg}");

    // now join the log thread
    logThread.join();

    // verify log target has 100 messages, later part of it has new format
    ASSERT_EQ(logTarget->getInfoLogMessages().size(), 100);
    bool formatChanged = false;
    for (uint32_t i = 0; i < 100; ++i) {
        if (logTarget->getInfoLogMessages()[i].compare("Test message") == 0) {
            ASSERT_EQ(formatChanged, false);
        } else {
            if (!formatChanged) {
                formatChanged = true;
            }
            ASSERT_EQ(logTarget->getInfoLogMessages()[i].compare("XXX Test message"), 0);
        }
    }
    ASSERT_EQ(formatChanged, true);

    elog::clearAllLogTargets();

    // restore log format to default
    elog::resetLogFormat();
}

TEST(ELogDynamicConfig, GlobalConfigReplaceFilter) {
    TestLogTarget* logTarget = new (std::nothrow) TestLogTarget();
    ASSERT_NE(logTarget, nullptr);

    bool res = logTarget->setLogFormat("${msg}");
    ASSERT_EQ(res, true);

    elog::ELogTargetId id = elog::addLogTarget(logTarget);
    ASSERT_NE(id, ELOG_INVALID_TARGET_ID);
    logTarget->clearLogMessages();

    // now run background thread to remove the log target at some point
    std::thread logThread = std::thread([]() {
        for (uint32_t i = 0; i < 100; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            ELOG_INFO("%u", i);
        }
    });

    // sleep a bit and replace the log filter
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    elog::setLogFilter(new (std::nothrow) elog::ELogCountFilter(2));

    // now join the log thread
    logThread.join();

    // verify log target has at most 100 messages, later part of it contains even messages
    ASSERT_LE(logTarget->getInfoLogMessages().size(), 100);
    bool formatChanged = false;
    uint32_t prevJ = 0;
    for (uint32_t i = 0; i < logTarget->getInfoLogMessages().size(); ++i) {
        uint32_t j = std::stoul(logTarget->getInfoLogMessages()[i]);
        if (j == i) {
            ASSERT_EQ(formatChanged, false);
        } else {
            if (!formatChanged) {
                formatChanged = true;
            } else {
                ASSERT_EQ(j, prevJ + 2);
            }
            prevJ = j;
        }
    }
    ASSERT_EQ(formatChanged, true);

    elog::clearAllLogTargets();

    // restore log filter to default
    elog::clearLogFilter();
}
#endif