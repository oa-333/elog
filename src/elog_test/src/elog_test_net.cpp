#include "elog_test_common.h"

#ifdef ELOG_ENABLE_NET
static int testTcp();
static int testUdp();
#endif
#ifdef ELOG_ENABLE_IPC
static int testPipe();
#endif

#if defined(ELOG_ENABLE_NET) || defined(ELOG_ENABLE_IPC)
#include "msg/elog_binary_format_provider.h"
#include "msg/elog_msg_server.h"
#include "msg/msg_server.h"

#ifdef ELOG_ENABLE_NET
#include "transport/tcp_server.h"
#include "transport/udp_server.h"
#endif

#ifdef ELOG_ENABLE_IPC
#include "transport/pipe_server.h"
#endif

static std::mutex sNetCoutLock;

std::atomic<uint64_t> sNetMsgCount;
static std::atomic<bool> sPrintNetMsg(false);
static void handleNetLogRecord(const elog_grpc::ELogRecordMsg* msg) {
    // TODO: conduct a real test - collect messages, verify they match the log messages
    sNetMsgCount.fetch_add(1, std::memory_order_relaxed);
    if (!sPrintNetMsg.load()) {
        return;
    }
    std::stringstream s;
    uint32_t fieldCount = 0;
    s << "Received log record: [";
    if (msg->has_recordid()) {
        s << "{rid = " << msg->recordid() << "}";
        ++fieldCount;
    }
    if (msg->has_timeunixepochmillis()) {
        if (fieldCount++ > 0) s << ", ";
        s << "utc = " << msg->timeunixepochmillis();
    }
    if (msg->has_hostname()) {
        if (fieldCount++ > 0) s << ", ";
        s << "host = " << msg->hostname();
    }
    if (msg->has_username()) {
        if (fieldCount++ > 0) s << ", ";
        s << "user = " << msg->username();
    }
    if (msg->has_programname()) {
        if (fieldCount++ > 0) s << ", ";
        s << "program = " << msg->programname();
    }
    if (msg->has_appname()) {
        if (fieldCount++ > 0) s << ", ";
        s << "app = " << msg->appname();
    }
    if (msg->has_processid()) {
        if (fieldCount++ > 0) s << ", ";
        s << "pid = " << msg->programname();
    }
    if (msg->has_threadid()) {
        if (fieldCount++ > 0) s << ", ";
        s << "tid = " << msg->threadid();
    }
    if (msg->has_threadname()) {
        if (fieldCount++ > 0) s << ", ";
        s << "tname = " << msg->threadname();
    }
    if (msg->has_logsourcename()) {
        if (fieldCount++ > 0) s << ", ";
        s << "source = " << msg->logsourcename();
    }
    if (msg->has_modulename()) {
        if (fieldCount++ > 0) s << ", ";
        s << "module = " << msg->modulename();
    }
    if (msg->has_file()) {
        if (fieldCount++ > 0) s << ", ";
        s << "file = " << msg->file();
    }
    if (msg->has_line()) {
        if (fieldCount++ > 0) s << ", ";
        s << "line = " << msg->line();
    }
    if (msg->has_functionname()) {
        if (fieldCount++ > 0) s << ", ";
        s << "function = " << msg->functionname();
    }
    if (msg->has_loglevel()) {
        if (fieldCount++ > 0) s << ", ";
        s << "log_level = " << msg->loglevel();
    }
    if (msg->has_logmsg()) {
        if (fieldCount++ > 0) s << ", ";
        s << "msg = " << msg->logmsg();
    }
    std::unique_lock<std::mutex> lock(sNetCoutLock);
    std::cout << s.str() << std::endl;
}

class TestServer : public elog::ELogMsgServer {
public:
    TestServer(const char* name) : elog::ELogMsgServer(name), m_dataServer(nullptr) {}
    ~TestServer() override {}

    bool initTestServer() {
        return initialize(m_dataServer, 10, 5, 1024) == commutil::ErrorCode::E_OK;
    }

protected:
    /** @brief Handle an incoming log record. */
    int handleLogRecordMsg(elog_grpc::ELogRecordMsg* logRecordMsg) override {
        handleNetLogRecord(logRecordMsg);
        // randomly delay response to test for resend crashes
        uint64_t r = rand() / ((double)RAND_MAX) * 20;
        // std::this_thread::sleep_for(std::chrono::milliseconds(r));
        return 0;
    }

    commutil::DataServer* m_dataServer;
};

#ifdef ELOG_ENABLE_NET
class TestTcpServer final : public TestServer {
public:
    TestTcpServer(const char* iface, int port)
        : TestServer("TCP"), m_tcpServer(iface, port, 5, 10) {
        m_dataServer = &m_tcpServer;
    }
    ~TestTcpServer() override {}

private:
    commutil::TcpServer m_tcpServer;
};

class TestUdpServer : public TestServer {
public:
    TestUdpServer(const char* iface, int port) : TestServer("UDP"), m_udpServer(iface, port, 60) {
        m_dataServer = &m_udpServer;
    }
    ~TestUdpServer() override {}

private:
    commutil::UdpServer m_udpServer;
};
#endif

#ifdef ELOG_ENABLE_IPC
class TestPipeServer : public TestServer {
public:
    TestPipeServer(const char* pipeName) : TestServer("Pipe"), m_pipeServer(pipeName, 5, 10) {
        m_dataServer = &m_pipeServer;
    }
    ~TestPipeServer() override {}

private:
    commutil::PipeServer m_pipeServer;
};
#endif
#endif

// TODO: test pre-init with string -vector log target
/*static void printPreInitMessages() {
    // this should trigger printing of pre-init messages
    elog::ELogTargetId id = elog::addStdErrLogTarget();
    elog::removeLogTarget(id);
    // elog::discardAccumulatedLogMessages();
}*/

#if defined(ELOG_ENABLE_NET) || defined(ELOG_ENABLE_IPC)

#define MSG_OPT_HAS_PRE_INIT 0x01
#define MSG_OPT_TRACE 0x02

static int sMsgCnt = -1;

int testMsgClient(TestServer& server, const char* schema, const char* serverType, const char* mode,
                  const char* address, bool compress = false, int opts = 0,
                  uint32_t stMsgCount = 1000, uint32_t mtMsgCount = 1000) {
    if (!server.initTestServer()) {
        DBGPRINT(stderr, "Failed to initialize test server\n");
        return 1;
    }
    if (server.start() != commutil::ErrorCode::E_OK) {
        DBGPRINT(stderr, "Failed to start test server\n");
        server.terminate();
        return 2;
    }
    std::this_thread::sleep_for(std::chrono::seconds(1));
    //  prepare log target URL and test name
    std::string cfg = std::string(schema) + "://" + serverType + "?mode=" + mode +
                      "&address=" + address + "&" +
                      "log_format=msg:${rid}, ${time}, ${level}, ${msg}&"
                      "binary_format=protobuf&compress=" +
                      (compress ? "yes" : "no") +
                      "&max_concurrent_requests=1024&"
                      "flush_policy=count&flush_count=1024";
    std::string testName = std::string(mode) + " " + serverType;
    std::string mtResultFileName = std::string("elog_test_") + mode + "_" + serverType;

    // run single threaded test
    double msgPerf = 0.0f;
    double ioPerf = 0.0f;

    sNetMsgCount.store(0, std::memory_order_relaxed);

    if (opts & MSG_OPT_TRACE) {
        elog::setReportLevel(elog::ELEVEL_TRACE);
    }

    runSingleThreadedTest(testName.c_str(), cfg.c_str(), msgPerf, ioPerf, TT_NORMAL, stMsgCount);
    uint32_t receivedMsgCount = (uint32_t)sNetMsgCount.load(std::memory_order_relaxed);
    // total: 2 pre-init + stMsgCount single-thread messages
    uint32_t totalMsg = stMsgCount;
    totalMsg += elog::getAccumulatedMessageCount();
    if (receivedMsgCount != totalMsg) {
        DBGPRINT(stderr,
                 "%s client single-thread test failed, missing messages on server side, expected "
                 "%u, got %u\n",
                 testName.c_str(), totalMsg, receivedMsgCount);
        server.stop();
        server.terminate();
        DBGPRINT(stderr, "%s client test FAILED\n", testName.c_str());
        return 1;
    }

    // multi-threaded test
    sMsgCnt = mtMsgCount;
    sNetMsgCount.store(0, std::memory_order_relaxed);
    runMultiThreadTest(testName.c_str(), mtResultFileName.c_str(), cfg.c_str(), TT_NORMAL,
                       mtMsgCount, 1, 4);
    sMsgCnt = 0;

    server.stop();
    server.terminate();

    receivedMsgCount = (uint32_t)sNetMsgCount.load(std::memory_order_relaxed);
    // total: sMsgCnt multi-thread messages + total threads
    // each test adds 2 more messages for start and end test phase
    // we run total 10 threads  in 4 phases(1 + 2 + 3 + 4)
    const uint32_t threadCount = 10;
    const uint32_t phaseCount = 4;
    const uint32_t exMsgPerPhase = 2;
    totalMsg = threadCount * mtMsgCount + exMsgPerPhase * phaseCount;
    totalMsg += elog::getAccumulatedMessageCount();
    if (receivedMsgCount != totalMsg) {
        DBGPRINT(
            stderr,
            "%s client multi-thread test failed, missing messages on server side, expected %u, "
            "got %u\n",
            testName.c_str(), totalMsg, receivedMsgCount);
        DBGPRINT(stderr, "%s client test FAILED\n", testName.c_str());
        return 2;
    }

    if (compress) {
        DBGPRINT(stderr, "%s client test (compressed) PASSED\n", testName.c_str());
    } else {
        DBGPRINT(stderr, "%s client test PASSED\n", testName.c_str());
    }
    return 0;
}

#endif

#ifdef ELOG_ENABLE_NET
static int testTcpSync(bool compress);
static int testUdpSync(bool compress);
static int testTcpAsync(bool compress);
static int testUdpAsync(bool compress);

TEST(ELogNet, TcpSync) {
    int res = testTcpSync(false);
    EXPECT_EQ(res, 0);
    elog::discardAccumulatedLogMessages();
}
TEST(ELogNet, TcpSyncCompress) {
    int res = testTcpSync(true);
    EXPECT_EQ(res, 0);
}
TEST(ELogNet, UdpSync) {
    int res = testUdpSync(false);
    EXPECT_EQ(res, 0);
}
TEST(ELogNet, UdpSyncCompress) {
    int res = testUdpSync(true);
    EXPECT_EQ(res, 0);
}
TEST(ELogNet, TcpAsync) {
    int res = testTcpAsync(false);
    EXPECT_EQ(res, 0);
}
TEST(ELogNet, TcpAsyncCompress) {
    int res = testTcpAsync(true);
    EXPECT_EQ(res, 0);
}
TEST(ELogNet, UdpAsync) {
    int res = testUdpAsync(false);
    EXPECT_EQ(res, 0);
}
TEST(ELogNet, UdpAsyncCompress) {
    int res = testUdpAsync(true);
    EXPECT_EQ(res, 0);
}

int testTcpSync(bool compress) {
    TestTcpServer server("0.0.0.0", 5051);
    DBGPRINT(stderr, "Server listening on port 5051\n");
    return testMsgClient(server, "net", "tcp", "sync", "127.0.0.1:5051", compress);
}

int testTcpAsync(bool compress) {
    TestTcpServer server("0.0.0.0", 5051);
    DBGPRINT(stderr, "Server listening on port 5051\n");
    // sPrintNetMsg.store(true);
    return testMsgClient(server, "net", "tcp", "async", "127.0.0.1:5051", compress);
}

int testUdpSync(bool compress) {
    TestUdpServer server("0.0.0.0", 5051);
    return testMsgClient(server, "net", "udp", "sync", "127.0.0.1:5051", compress);
}

int testUdpAsync(bool compress) {
    TestUdpServer server("0.0.0.0", 5051);
    return testMsgClient(server, "net", "udp", "async", "127.0.0.1:5051", compress);
}
#endif

#ifdef ELOG_ENABLE_IPC

static int testPipeSync(bool compress);
static int testPipeAsync(bool compress);

TEST(ELogIpc, PipeSync) {
    int res = testPipeSync(false);
    EXPECT_EQ(res, 0);
    elog::discardAccumulatedLogMessages();
}
TEST(ELogIpc, PipeSyncCompress) {
    int res = testPipeSync(true);
    EXPECT_EQ(res, 0);
}
TEST(ELogIpc, PipeAsync) {
    int res = testPipeAsync(false);
    EXPECT_EQ(res, 0);
    elog::discardAccumulatedLogMessages();
}
TEST(ELogIpc, PipeAsyncCompress) {
    int res = testPipeAsync(true);
    EXPECT_EQ(res, 0);
}

int testPipeSync(bool compress) {
    TestPipeServer server("elog_test_pipe");
    DBGPRINT(stderr, "Server listening on pipe elog_test_pipe\n");
    return testMsgClient(server, "ipc", "pipe", "sync", "elog_test_pipe", compress);
}

int testPipeAsync(bool compress) {
    TestPipeServer server("elog_test_pipe");
    DBGPRINT(stderr, "Server listening on pipe elog_test_pipe\n");
    return testMsgClient(server, "ipc", "pipe", "async", "elog_test_pipe", compress);
}
#endif