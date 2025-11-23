#include <sstream>

#include "elog_test_common.h"

#ifdef ELOG_ENABLE_STACK_TRACE
static bool verifyStackTrace(const std::vector<std::string>& logMessages) {
    // we tokenize the stack trace into lines
    const std::string& stackTrace = logMessages[1];
    std::vector<std::string> lines;
    tokenize(stackTrace.c_str(), lines, "\r\n");
    EXPECT_GE(lines.size(), 3);

    std::string logMsg =
        std::string("Testing stack trace for thread ") + std::to_string(getCurrentThreadId());
    EXPECT_EQ(logMessages[0].compare(logMsg), 0);
    EXPECT_EQ(lines[0].compare("some test title 1:"), 0);

    std::string hexStr;
    std::stringstream ss;
    ss << std::hex << getCurrentThreadId();
    hexStr = ss.str();

    std::string threadBanner = std::string("[Thread ") + std::to_string(getCurrentThreadId()) +
                               " (0x" + hexStr + ") <" + elog::getCurrentThreadName() +
                               "> stack trace]";
    EXPECT_EQ(lines[1].compare(threadBanner), 0);
    return true;
}

TEST(ELogMisc, StackTrace) {
    TestLogTarget* logTarget = new (std::nothrow) TestLogTarget();
    logTarget->setLogFormat("${msg}");
    elog::addLogTarget(logTarget);

    // since error messages may slip in from other threads (e.g. publish thread), we use info log
    // messages
    const auto& logMessages = logTarget->getInfoLogMessages();

    logTarget->clearLogMessages();
    ELOG_STACK_TRACE(elog::ELEVEL_INFO, "some test title 1", 0, "Testing stack trace for thread %u",
                     getCurrentThreadId());
    EXPECT_EQ(logMessages.size(), 2);
    EXPECT_EQ(verifyStackTrace(logMessages), true);
    fprintf(stderr, "Seeing %zu log messages\n", logMessages.size());
    for (const auto& msg : logMessages) {
        fprintf(stderr, "%s\n", msg.c_str());
    }

    logTarget->clearLogMessages();
#ifndef ELOG_LINUX
    ELOG_APP_STACK_TRACE(elog::ELEVEL_INFO, "some test title 2", 0,
                         "Testing app stack trace for thread %u", getCurrentThreadId());
#endif
    /*fprintf(stderr, "Seeing %zu log messages\n", logMessages.size());
    for (const auto& msg : logMessages) {
        fprintf(stderr, "%s\n", msg.c_str());
    }*/

    elog::removeLogTarget(logTarget);
}
#endif