#include <sstream>

#include "elog_test_common.h"

#ifdef ELOG_ENABLE_JSON
#include <nlohmann/json.hpp>
#endif

TEST(ELogMisc, ThreadName) {
    TestLogTarget* logTarget = new (std::nothrow) TestLogTarget();
    logTarget->setLogFormat("${tname}");
    elog::addLogTarget(logTarget);

    const auto& logMessages = logTarget->getLogMessages();

    logTarget->clearLogMessages();
    elog::setCurrentThreadName("elog_test_main2");
    ELOG_INFO("Test thread name/id, expecting elog_test_main2/%u", getCurrentThreadId());
    EXPECT_EQ(logMessages.size(), 1);
    EXPECT_EQ(logMessages[0].compare("elog_test_main2"), 0);

    logTarget->clearLogMessages();
    std::thread t = std::thread([logTarget]() {
        elog::setCurrentThreadName("another_thread");
        ELOG_INFO("Test thread name/id, expecting another_thread/%u", getCurrentThreadId());
    });

    t.join();
    EXPECT_EQ(logMessages.size(), 1);
    EXPECT_EQ(logMessages[0].compare("another_thread"), 0);

    elog::removeLogTarget(logTarget);
}

TEST(ELogMisc, LogMacros) {
    TestLogTarget* logTarget = new (std::nothrow) TestLogTarget();
    logTarget->setLogFormat("${msg}");
    elog::addLogTarget(logTarget);

    // since error messages may slip in from other threads (e.g. publish thread), we use info log
    // messages
    const auto& logMessages = logTarget->getInfoLogMessages();

    // test once macro
    logTarget->clearLogMessages();
    for (uint32_t i = 0; i < 10; ++i) {
        ELOG_ONCE_INFO("This is a test once message");
    }
    EXPECT_EQ(logMessages.size(), 1);
    EXPECT_EQ(logMessages[0].compare("This is a test once message"), 0);

    // test once thread macro
    logTarget->clearLogMessages();
    for (uint32_t i = 0; i < 10; ++i) {
        ELOG_ONCE_THREAD_INFO("This is a test once thread message");
    }
    EXPECT_EQ(logMessages.size(), 1);
    EXPECT_EQ(logMessages[0].compare("This is a test once thread message"), 0);

    // test moderate macro
    logTarget->clearLogMessages();
    for (uint32_t i = 0; i < 30; ++i) {
        ELOG_MODERATE_INFO(2, 1, elog::ELogTimeUnits::TU_SECONDS,
                           "This is a test moderate message (twice per second)");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    // 30 messages in 3 seconds, but only twice per second allowed so we should get roughly 6
    // messages, but with some deviation on the sides it could get as much as 10, but not less than
    // 5
    EXPECT_GE(logMessages.size(), 5);
    EXPECT_LE(logMessages.size(), 10);
    EXPECT_EQ(logMessages.front().compare("This is a test moderate message (twice per second)"), 0);
    EXPECT_EQ(logMessages.back().compare("This is a test moderate message (twice per second)"), 0);

    // test every-N macro
    logTarget->clearLogMessages();
    for (uint32_t i = 0; i < 30; ++i) {
        ELOG_EVERY_N_INFO(10, "This is a test every-N message (one in 10 messages, total 30)");
    }
    EXPECT_EQ(logMessages.size(), 3);
    EXPECT_EQ(
        logMessages.back().compare("This is a test every-N message (one in 10 messages, total 30)"),
        0);

    elog::removeLogTarget(logTarget);
}

#ifdef ELOG_ENABLE_JSON
TEST(ELogMisc, StructuredLogging) {
    // test structured logging in JSON format
    TestLogTarget* logTarget = new (std::nothrow) TestLogTarget();
    logTarget->setLogFormat(
        "{\n"
        "\t\"time\": ${time_epoch},\n"
        "\t\"level\": \"${level}\",\n"
        "\t\"thread_id\": ${tid},\n"
        "\t\"log_source\": \"${src}\",\n"
        "\t\"log_msg\": \"${msg}\"\n"
        "}");
    elog::addLogTarget(logTarget);

    const auto& logMessages = logTarget->getLogMessages();

    logTarget->clearLogMessages();
    ELOG_INFO("This is a test message");
    EXPECT_EQ(logMessages.size(), 1);
    ELOG_DEBUG_EX(sTestLogger, "Got message: %s\n", logMessages[0].c_str());
    nlohmann::json jsonLog = nlohmann::json::parse(logMessages[0]);
    EXPECT_EQ(jsonLog.is_object(), true);
    EXPECT_EQ(jsonLog.contains("time"), true);
    EXPECT_EQ(jsonLog.contains("level"), true);
    EXPECT_EQ(jsonLog.contains("thread_id"), true);
    EXPECT_EQ(jsonLog.contains("log_source"), true);
    EXPECT_EQ(jsonLog.contains("log_msg"), true);

    EXPECT_EQ(jsonLog["time"].is_number_integer(), true);
    EXPECT_EQ(jsonLog["level"].is_string(), true);
    EXPECT_EQ(jsonLog["thread_id"].is_number_integer(), true);
    EXPECT_EQ(jsonLog["log_source"].is_string(), true);
    EXPECT_EQ(jsonLog["log_msg"].is_string(), true);

    EXPECT_EQ(jsonLog["level"].get<std::string>().compare("INFO"), 0);
    EXPECT_EQ(jsonLog["thread_id"].get<uint32_t>(), getCurrentThreadId());
    EXPECT_EQ(jsonLog["log_source"].get<std::string>().compare("elog_root"), 0);
    EXPECT_EQ(jsonLog["log_msg"].get<std::string>().compare("This is a test message"), 0);

    elog::removeLogTarget(logTarget);
}
#endif