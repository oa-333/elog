#include <regex>

#include "elog_test_common.h"

#ifdef ELOG_WINDOWS
#include <winsock2.h>
#else
#include <pwd.h>
#include <sys/types.h>
#endif

#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX 256  // A common maximum length if not defined
#endif

#ifndef LOGIN_NAME_MAX
#define LOGIN_NAME_MAX 256  // A common maximum length if not defined
#endif

static void getUserName(std::string& userName) {
    bool res = true;
    char userNameBuf[LOGIN_NAME_MAX + 1];
#ifdef ELOG_WINDOWS
    DWORD len = LOGIN_NAME_MAX;
    if (!GetUserNameA(userNameBuf, &len)) {
        res = false;
    }
#else  // ELOG_WINDOWS
    if (getlogin_r(userNameBuf, LOGIN_NAME_MAX) != 0) {
        // try from geteuid
        uid_t uid = geteuid();
        struct passwd* pw = getpwuid(uid);
        if (pw != nullptr) {
            strncpy(userNameBuf, pw->pw_name, LOGIN_NAME_MAX);
        } else {
            res = false;
        }
    }
#endif
    // try environment variable USERNAME or USER
    if (res) {
        userNameBuf[LOGIN_NAME_MAX] = 0;
        userName = userNameBuf;
    } else {
        std::string userName;
        if (getenv("USERNAME") != nullptr) {
            userName = getenv("USERNAME");
        } else if (getenv("USER") != nullptr) {
            userName = getenv("USER");
        } else {
            userName = "<N/A>";
        }
    }
}

TEST(ELogCore, LogLevels) {
    // make sure log level filtering works
    TestLogTarget* logTarget = new (std::nothrow) TestLogTarget();
    logTarget->setLogFormat("${level}");
    elog::addLogTarget(logTarget);
    const auto& logMessages = logTarget->getLogMessages();

    // start with all log levels enabled
    elog::getRootLogSource()->setLogLevel(elog::ELEVEL_DIAG, elog::ELogPropagateMode::PM_NONE);

    std::vector<elog::ELogLevel> levels = {
        elog::ELEVEL_FATAL, elog::ELEVEL_ERROR, elog::ELEVEL_WARN,  elog::ELEVEL_NOTICE,
        elog::ELEVEL_INFO,  elog::ELEVEL_TRACE, elog::ELEVEL_DEBUG, elog::ELEVEL_DIAG};
    EXPECT_EQ(levels.size(), ELEVEL_COUNT);

    // issue log records and make sure all levels are printed
    logTarget->clearLogMessages();
    ELOG_FATAL("Test message");
    ELOG_ERROR("Test message");
    ELOG_WARN("Test message");
    ELOG_NOTICE("Test message");
    ELOG_INFO("Test message");
    ELOG_TRACE("Test message");
    ELOG_DEBUG("Test message");
    ELOG_DIAG("Test message");
    EXPECT_EQ(logMessages.size(), ELEVEL_COUNT);
    for (uint32_t i = 0; i < ELEVEL_COUNT; ++i) {
        EXPECT_EQ(logMessages[i].compare(elog::elogLevelToStr(levels[i])), 0);
    }

    // set log level to trace and do it again
    elog::getRootLogSource()->setLogLevel(elog::ELEVEL_TRACE, elog::ELogPropagateMode::PM_NONE);

    // issue log records and make sure all levels are printed
    logTarget->clearLogMessages();
    ELOG_FATAL("Test message");
    ELOG_ERROR("Test message");
    ELOG_WARN("Test message");
    ELOG_NOTICE("Test message");
    ELOG_INFO("Test message");
    ELOG_TRACE("Test message");
    ELOG_DEBUG("Test message");
    ELOG_DIAG("Test message");
    EXPECT_EQ(logMessages.size(), ELEVEL_COUNT - 2);
    for (uint32_t i = 0; i < ELEVEL_COUNT - 2; ++i) {
        EXPECT_EQ(logMessages[i].compare(elog::elogLevelToStr(levels[i])), 0);
    }

    // now allow only fatal
    elog::getRootLogSource()->setLogLevel(elog::ELEVEL_FATAL, elog::ELogPropagateMode::PM_NONE);

    // issue log records and make sure all levels are printed
    logTarget->clearLogMessages();
    ELOG_FATAL("Test message");
    ELOG_ERROR("Test message");
    ELOG_WARN("Test message");
    ELOG_NOTICE("Test message");
    ELOG_INFO("Test message");
    ELOG_TRACE("Test message");
    ELOG_DEBUG("Test message");
    ELOG_DIAG("Test message");
    EXPECT_EQ(logMessages.size(), 1);
    EXPECT_EQ(logMessages[0].compare(elog::elogLevelToStr(levels[0])), 0);

    // since this test messes up with global log statistics, we need to reset it
    elog::resetLogStatistics();
}

TEST(ELogCore, LogFields) {
    TestLogTarget* logTarget = new (std::nothrow) TestLogTarget();
    elog::addLogTarget(logTarget);
    const auto& logMessages = logTarget->getLogMessages();

    elog::getRootLogSource()->setLogLevel(elog::ELEVEL_INFO, elog::ELogPropagateMode::PM_NONE);

    // check record id
    logTarget->setLogFormat("${rid}");
    logTarget->clearLogMessages();
    ELOG_INFO("Test message");
    ELOG_INFO("Test message");
    ELOG_INFO("Test message");
    EXPECT_EQ(logMessages.size(), 3);
    uint64_t rid = std::stoll(logMessages[0]);
    EXPECT_EQ(std::stoll(logMessages[1]), rid + 1);
    EXPECT_EQ(std::stoll(logMessages[2]), rid + 2);

    // check time field
    logTarget->setLogFormat("${time}");
    logTarget->clearLogMessages();
    ELOG_INFO("Test message");
    EXPECT_EQ(logMessages.size(), 1);
    std::regex pattern("\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2}.\\d{3}");
    EXPECT_EQ(std::regex_match(logMessages[0], pattern), true);

    // check time epoch
    logTarget->setLogFormat("${time_epoch}");
    logTarget->clearLogMessages();
    ELOG_INFO("Test message");
    EXPECT_EQ(logMessages.size(), 1);
    // time epoch is in micros by default
    int64_t epochMicros = std::chrono::duration_cast<std::chrono::microseconds>(
                              std::chrono::system_clock::now().time_since_epoch())
                              .count();
    int64_t logEpoch = std::strtoll(logMessages[0].c_str(), nullptr, 10);
    // expect no more than 10 millis variance
    EXPECT_LE(std::abs(epochMicros - logEpoch), 10000);

    // check host name
    logTarget->setLogFormat("${host}");
    logTarget->clearLogMessages();
    ELOG_INFO("Test message");
    EXPECT_EQ(logMessages.size(), 1);
    char hostname[HOST_NAME_MAX];
    gethostname(hostname, sizeof(hostname));
    EXPECT_EQ(logMessages[0].compare(hostname), 0);

    // check user name
    logTarget->setLogFormat("${user}");
    logTarget->clearLogMessages();
    ELOG_INFO("Test message");
    EXPECT_EQ(logMessages.size(), 1);
    std::string userName;
    getUserName(userName);
    EXPECT_EQ(logMessages[0].compare(userName), 0);

    // os name and version are currently not tested

    // check app name
    logTarget->setLogFormat("${app}");
    logTarget->clearLogMessages();
    elog::setAppName("test-app-name");
    ELOG_INFO("Test message");
    EXPECT_EQ(logMessages.size(), 1);
    EXPECT_EQ(logMessages[0].compare("test-app-name"), 0);

    // check program name
    logTarget->setLogFormat("${prog}");
    logTarget->clearLogMessages();
    ELOG_INFO("Test message");
    EXPECT_EQ(logMessages.size(), 1);
#ifdef ELOG_MINGW
    EXPECT_EQ(logMessages[0].compare("elog_test_mingw"), 0);
#else
    EXPECT_EQ(logMessages[0].compare("elog_test"), 0);
#endif

    // check pid
    logTarget->setLogFormat("${pid}");
    logTarget->clearLogMessages();
    ELOG_INFO("Test message");
    EXPECT_EQ(logMessages.size(), 1);
    EXPECT_EQ(logMessages[0].compare(std::to_string(getpid())), 0);

    // check thread id
    logTarget->setLogFormat("${tid}");
    logTarget->clearLogMessages();
    ELOG_INFO("Test message");
    EXPECT_EQ(logMessages.size(), 1);
    EXPECT_EQ(logMessages[0].compare(std::to_string(getCurrentThreadId())), 0);

    // check thread name
    logTarget->setLogFormat("${tname}");
    logTarget->clearLogMessages();
    elog::setCurrentThreadName("elog-test-thread");
    ELOG_INFO("Test message");
    EXPECT_EQ(logMessages.size(), 1);
    EXPECT_EQ(logMessages[0].compare("elog-test-thread"), 0);

    // check source name
    logTarget->setLogFormat("${src}");
    logTarget->clearLogMessages();
    elog::ELogLogger* logger = elog::getPrivateLogger("elog.test.core.log.fields");
    ELOG_INFO_EX(logger, "Test message");
    EXPECT_EQ(logMessages.size(), 1);
    EXPECT_EQ(logMessages[0].compare("elog.test.core.log.fields"), 0);

    // check module name
    logTarget->setLogFormat("${mod}");
    logTarget->clearLogMessages();
    logger->getLogSource()->setModuleName("elog_test_module");
    ELOG_INFO_EX(logger, "Test message");
    EXPECT_EQ(logMessages.size(), 1);
    EXPECT_EQ(logMessages[0].compare("elog_test_module"), 0);

    // check file name
    logTarget->setLogFormat("${file}");
    logTarget->clearLogMessages();
    ELOG_INFO("Test message");
    EXPECT_EQ(logMessages.size(), 1);
    EXPECT_NE(logMessages[0].find("elog_test_core.cpp"), std::string::npos);

    // check line number
    logTarget->setLogFormat("${line}");
    logTarget->clearLogMessages();
    // clang-format off
    ELOG_INFO("Test message"); int line = __LINE__;
    // clang-format on
    EXPECT_EQ(logMessages.size(), 1);
    int lineNumber = std::stoi(logMessages[0]);
    EXPECT_EQ(lineNumber, line);

    // check function name
    logTarget->setLogFormat("${func}");
    logTarget->clearLogMessages();
    ELOG_INFO("Test message");
    EXPECT_EQ(logMessages.size(), 1);
    EXPECT_NE(logMessages[0].find("ELogCore"), std::string::npos);
    EXPECT_NE(logMessages[0].find("LogFields"), std::string::npos);

    // check log level
    logTarget->setLogFormat("${level}");
    logTarget->clearLogMessages();
    ELOG_INFO("Test message");
    EXPECT_EQ(logMessages.size(), 1);
    EXPECT_EQ(logMessages[0].compare("INFO"), 0);

    // check log message
    logTarget->setLogFormat("${msg}");
    logTarget->clearLogMessages();
    ELOG_INFO("Test message");
    EXPECT_EQ(logMessages.size(), 1);
    EXPECT_EQ(logMessages[0].compare("Test message"), 0);

    // check env var
    // caller script is required to set env var TEST_ENV_VAR=TEST_ENV_VALUE
    logTarget->setLogFormat("${env:name=TEST_ENV_VAR}");
    logTarget->clearLogMessages();
    ELOG_INFO("Test message");
    EXPECT_EQ(logMessages.size(), 1);
    EXPECT_EQ(logMessages[0].compare("TEST_ENV_VALUE"), 0);
}

TEST(ELogCore, TimeFormat) {
    TestLogTarget* logTarget = new (std::nothrow) TestLogTarget();
    elog::addLogTarget(logTarget);
    const auto& logMessages = logTarget->getLogMessages();

    elog::getRootLogSource()->setLogLevel(elog::ELEVEL_INFO, elog::ELogPropagateMode::PM_NONE);

    // check local time without millis
    bool res = logTarget->setLogFormat("${time:seconds}");
    EXPECT_EQ(res, true);
    logTarget->clearLogMessages();
    ELOG_INFO("Test message");
    EXPECT_EQ(logMessages.size(), 1);
    fprintf(stderr, "Time: %s\n", logMessages[0].c_str());

    // check local time without millis
    res = logTarget->setLogFormat("${time:millis}");
    EXPECT_EQ(res, true);
    logTarget->clearLogMessages();
    ELOG_INFO("Test message");
    EXPECT_EQ(logMessages.size(), 1);
    fprintf(stderr, "Time: %s\n", logMessages[0].c_str());

    // check local time without millis
    res = logTarget->setLogFormat("${time:micros}");
    EXPECT_EQ(res, true);
    logTarget->clearLogMessages();
    ELOG_INFO("Test message");
    EXPECT_EQ(logMessages.size(), 1);
    fprintf(stderr, "Time: %s\n", logMessages[0].c_str());

    // check local time without millis
    res = logTarget->setLogFormat("${time:nanos}");
    EXPECT_EQ(res, true);
    logTarget->clearLogMessages();
    ELOG_INFO("Test message");
    EXPECT_EQ(logMessages.size(), 1);
    fprintf(stderr, "Time: %s\n", logMessages[0].c_str());

    // check local time with zone
    res = logTarget->setLogFormat("${time:zone}");
    EXPECT_EQ(res, true);
    logTarget->clearLogMessages();
    ELOG_INFO("Test message");
    EXPECT_EQ(logMessages.size(), 1);
    fprintf(stderr, "Time: %s\n", logMessages[0].c_str());

    // check local time with zone but without millis
    res = logTarget->setLogFormat("${time:zone:seconds}");
    EXPECT_EQ(res, true);
    logTarget->clearLogMessages();
    ELOG_INFO("Test message");
    EXPECT_EQ(logMessages.size(), 1);
    fprintf(stderr, "Time: %s\n", logMessages[0].c_str());

    // check local time with zone but without millis
    res = logTarget->setLogFormat("${time:zone:millis}");
    EXPECT_EQ(res, true);
    logTarget->clearLogMessages();
    ELOG_INFO("Test message");
    EXPECT_EQ(logMessages.size(), 1);
    fprintf(stderr, "Time: %s\n", logMessages[0].c_str());

    // check local time with zone but without millis
    res = logTarget->setLogFormat("${time:zone:micros}");
    EXPECT_EQ(res, true);
    logTarget->clearLogMessages();
    ELOG_INFO("Test message");
    EXPECT_EQ(logMessages.size(), 1);
    fprintf(stderr, "Time: %s\n", logMessages[0].c_str());

    // check local time with zone but without millis
    res = logTarget->setLogFormat("${time:zone:nanos}");
    EXPECT_EQ(res, true);
    logTarget->clearLogMessages();
    ELOG_INFO("Test message");
    EXPECT_EQ(logMessages.size(), 1);
    fprintf(stderr, "Time: %s\n", logMessages[0].c_str());

    // check global time
    res = logTarget->setLogFormat("${time:global}");
    EXPECT_EQ(res, true);
    logTarget->clearLogMessages();
    ELOG_INFO("Test message");
    EXPECT_EQ(logMessages.size(), 1);
    fprintf(stderr, "Time: %s\n", logMessages[0].c_str());

    // check global time
    res = logTarget->setLogFormat("${time:global:seconds}");
    EXPECT_EQ(res, true);
    logTarget->clearLogMessages();
    ELOG_INFO("Test message");
    EXPECT_EQ(logMessages.size(), 1);
    fprintf(stderr, "Time: %s\n", logMessages[0].c_str());

    // check global time
    res = logTarget->setLogFormat("${time:global:millis}");
    EXPECT_EQ(res, true);
    logTarget->clearLogMessages();
    ELOG_INFO("Test message");
    EXPECT_EQ(logMessages.size(), 1);
    fprintf(stderr, "Time: %s\n", logMessages[0].c_str());

    // check global time
    res = logTarget->setLogFormat("${time:global:micros}");
    EXPECT_EQ(res, true);
    logTarget->clearLogMessages();
    ELOG_INFO("Test message");
    EXPECT_EQ(logMessages.size(), 1);
    fprintf(stderr, "Time: %s\n", logMessages[0].c_str());

    // check global time
    res = logTarget->setLogFormat("${time:global:nanos}");
    EXPECT_EQ(res, true);
    logTarget->clearLogMessages();
    ELOG_INFO("Test message");
    EXPECT_EQ(logMessages.size(), 1);
    fprintf(stderr, "Time: %s\n", logMessages[0].c_str());

    // check global time with zone
    res = logTarget->setLogFormat("${time:global:zone}");
    EXPECT_EQ(res, true);
    logTarget->clearLogMessages();
    ELOG_INFO("Test message");
    EXPECT_EQ(logMessages.size(), 1);
    fprintf(stderr, "Time: %s\n", logMessages[0].c_str());

    // check global time without milliseconds
    res = logTarget->setLogFormat("${time:global:zone:seconds}");
    EXPECT_EQ(res, true);
    logTarget->clearLogMessages();
    ELOG_INFO("Test message");
    EXPECT_EQ(logMessages.size(), 1);
    fprintf(stderr, "Time: %s\n", logMessages[0].c_str());

    // check global time with zone and without milliseconds
    res = logTarget->setLogFormat("${time:global:zone:millis}");
    EXPECT_EQ(res, true);
    logTarget->clearLogMessages();
    ELOG_INFO("Test message");
    EXPECT_EQ(logMessages.size(), 1);
    fprintf(stderr, "Time: %s\n", logMessages[0].c_str());

    // check global time with zone and without milliseconds
    res = logTarget->setLogFormat("${time:global:zone:micros}");
    EXPECT_EQ(res, true);
    logTarget->clearLogMessages();
    ELOG_INFO("Test message");
    EXPECT_EQ(logMessages.size(), 1);
    fprintf(stderr, "Time: %s\n", logMessages[0].c_str());

    // check global time with zone and without milliseconds
    res = logTarget->setLogFormat("${time:global:zone:nanos}");
    EXPECT_EQ(res, true);
    logTarget->clearLogMessages();
    ELOG_INFO("Test message");
    EXPECT_EQ(logMessages.size(), 1);
    fprintf(stderr, "Time: %s\n", logMessages[0].c_str());

    // check global time with zone and without milliseconds
    res = logTarget->setLogFormat("${time:format=\"%Y-%m-%d %H:%M:%S %Z %Ez\":nanos}");
    EXPECT_EQ(res, true);
    logTarget->clearLogMessages();
    ELOG_INFO("Test message");
    EXPECT_EQ(logMessages.size(), 1);
    fprintf(stderr, "Time: %s\n", logMessages[0].c_str());
}