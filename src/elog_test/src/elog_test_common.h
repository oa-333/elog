#ifndef __ELOG_TEST_COMMON_H__
#define __ELOG_TEST_COMMON_H__

#include <gtest/gtest.h>

// include elog system first, then any possible connector
#ifdef ELOG_USING_DBG_UTIL
#include "dbg_util.h"
#endif

#include "elog_api.h"

#define MT_MSG_COUNT 10000ull
#define ST_MSG_COUNT 1000000ull
#define MIN_THREAD_COUNT 1ul
#define MAX_THREAD_COUNT 16ul
#define DEFAULT_CFG "file:///./test_data/elog_test.log"

extern elog::ELogLogger* sTestLogger;

#ifdef ELOG_USING_DBG_UTIL
inline uint32_t getCurrentThreadId() { return dbgutil::getCurrentThreadId(); }
#else
extern uint32_t getCurrentThreadId();
#endif

extern void pinThread(uint32_t coreId);

extern void tokenize(const char* str, std::vector<std::string>& tokens,
                     const char* delims = " \t\r\n");

extern bool initTestEnv();
extern void termTestEnv();

extern bool execProcess(const char* cmd, std::string& outputRes);

extern elog::ELogTarget* initElog(const char* cfg = DEFAULT_CFG);
extern void termELog();

inline void getEnvVar(const char* name, std::string& value) {
    char* envVarValueLocal = getenv(name);
    if (envVarValueLocal != nullptr) {
        value = envVarValueLocal;
    }
}

inline bool isCaughtUp(elog::ELogTarget* logTarget, uint64_t targetMsgCount) {
    bool caughtUp = false;
    return logTarget->isCaughtUp(targetMsgCount, caughtUp) && caughtUp;
}

class ELogEnvironment : public ::testing::Environment {
public:
    ~ELogEnvironment() override {}

    // Override this to define how to set up the environment.
    void SetUp() override { initTestEnv(); }

    // Override this to define how to tear down the environment.
    void TearDown() override { termTestEnv(); }
};

class ELogTest : public testing::Test {
protected:
    ELogTest() {}
    ~ELogTest() override {}

    // If the constructor and destructor are not enough for setting up
    // and cleaning up each test, you can define the following methods:

    void SetUp() override {
        // Code here will be called immediately after the constructor (right
        // before each test).
        initElog();
    }

    void TearDown() override {
        // Code here will be called immediately after each test (right
        // before the destructor).
        termELog();
    }

    // Class members declared here can be used by all tests in the test suite
    // for Foo.
};

enum ThreadTestType { TT_NORMAL, TT_BINARY, TT_BINARY_CACHED, TT_BINARY_PRE_CACHED };

extern void runSingleThreadedTest(const char* title, const char* cfg, double& msgThroughput,
                                  double& ioThroughput, ThreadTestType testType = TT_NORMAL,
                                  uint32_t msgCount = ST_MSG_COUNT, bool enableTrace = false);

extern void runMultiThreadTest(const char* title, const char* fileName, const char* cfg,
                               ThreadTestType testType = TT_NORMAL,
                               uint32_t msgCount = MT_MSG_COUNT,
                               uint32_t minThreads = MIN_THREAD_COUNT,
                               uint32_t maxThreads = MAX_THREAD_COUNT, bool privateLogger = true,
                               bool enableTrace = false);

#define ELOG_BEGIN_TEST()            \
    elog::ELogStatistics startStats; \
    elog::getLogStatistics(startStats);

#define ELOG_END_TEST()               \
    elog::ELogStatistics endStats;    \
    elog::getLogStatistics(endStats); \
    return verifyNoErrors(startStats, endStats);

extern bool verifyNoErrors(const elog::ELogStatistics& startStats,
                           const elog::ELogStatistics& endStats);

class TestLogTarget : public elog::ELogTarget {
public:
    TestLogTarget() : ELogTarget("test") {}
    TestLogTarget(const TestLogTarget&) = delete;
    TestLogTarget(TestLogTarget&&) = delete;
    TestLogTarget& operator=(const TestLogTarget&) = delete;

    ELOG_DECLARE_LOG_TARGET(TestLogTarget)

    const std::vector<std::string>& getLogMessages() const { return m_logMessages; }

    void clearLogMessages() { m_logMessages.clear(); }

    std::mutex& getLock() { return m_lock; };

protected:
    /** @brief Order the log target to start (required for threaded targets). */
    bool startLogTarget() override { return true; }

    /** @brief Order the log target to stop (thread-safe). */
    bool stopLogTarget() override { return true; }

    /** @brief If not overriding @ref writeLogRecord(), then this method must be implemented. */
    virtual bool logFormattedMsg(const char* formattedLogMsg, size_t length) {
        std::unique_lock<std::mutex> lock(m_lock);
        m_logMessages.push_back(formattedLogMsg);
        return true;
    }

    /** @brief Orders a buffered log target to flush it log messages. */
    bool flushLogTarget() override { return true; }

private:
    std::mutex m_lock;
    std::vector<std::string> m_logMessages;
};

#endif  // __ELOG_TEST_COMMON_H__