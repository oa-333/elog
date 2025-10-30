#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <condition_variable>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <thread>

#include "elog_test_common.h"

#ifdef ELOG_ENABLE_GRPC_CONNECTOR
#include "rpc/elog_grpc_target.h"
#endif

static void testPerfPrivateLog();
static void testPerfSharedLogger();

// test connectors
#ifdef ELOG_ENABLE_GRPC_CONNECTOR
static int testGRPC();
static int testGRPCSimple();
static int testGRPCStream();
static int testGRPCAsync();
static int testGRPCAsyncCallbackUnary();
static int testGRPCAsyncCallbackStream();
#endif

#ifdef ELOG_ENABLE_GRPC_CONNECTOR
#include <grpc/grpc.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>

static std::mutex sGrpcCoutLock;

std::atomic<uint64_t> sGrpcMsgCount;
static void handleGrpcLogRecord(const elog_grpc::ELogRecordMsg* msg) {
    // TODO: conduct a real test - collect messages, verify they match the log messages
    sGrpcMsgCount.fetch_add(1, std::memory_order_relaxed);
    return;
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
    std::unique_lock<std::mutex> lock(sGrpcCoutLock);
    std::cout << s.str() << std::endl;
}

class TestGRPCServer final : public elog_grpc::ELogService::Service {
public:
    TestGRPCServer() {}

    ::grpc::Status SendLogRecord(::grpc::ServerContext* context,
                                 const ::elog_grpc::ELogRecordMsg* request,
                                 ::elog_grpc::ELogStatusMsg* response) final {
        handleGrpcLogRecord(request);
        return grpc::Status::OK;
    }

    ::grpc::Status StreamLogRecords(::grpc::ServerContext* context,
                                    ::grpc::ServerReader< ::elog_grpc::ELogRecordMsg>* reader,
                                    ::elog_grpc::ELogStatusMsg* response) {
        elog_grpc::ELogRecordMsg msg;
        while (reader->Read(&msg)) {
            handleGrpcLogRecord(&msg);
        }
        return grpc::Status::OK;
    }
};

class TestGRPCAsyncServer final : public elog_grpc::ELogService::AsyncService {
public:
    TestGRPCAsyncServer() {}

    void HandleRpcs(grpc::ServerCompletionQueue* cq) {
        // Spawn a new CallData instance to serve new clients.
        new CallData(this, cq);
        void* tag;  // uniquely identifies a request.
        bool ok;
        while (true) {
            // Block waiting to read the next event from the completion queue. The
            // event is uniquely identified by its tag, which in this case is the
            // memory address of a CallData instance.
            // The return value of Next should always be checked. This return value
            // tells us whether there is any kind of event or cq_ is shutting down.
            if (!cq->Next(&tag, &ok)) {
                break;
            }
            if (!ok) {
                break;
            }
            static_cast<CallData*>(tag)->Proceed();
        }
    }

private:
    // Class encompassing the state and logic needed to serve a request.
    // Code adapted from examples on the internet, but logic is not intuitive, especially spawning
    // child call data, and deleting this call data
    // actual phases are like this:
    // 1. request for incoming message
    // 2. wait for next event on completion queue:
    //      - either incoming message on the completion queue arrived
    //      - or a response sending has finished
    // 3. handle event: incoming message arrived, or response send finished
    // 4. for incoming message:
    //          handle incoming message
    //          spawn a new request as in step 1
    //          async send response to client
    //    for send finished:
    //          delete call data object
    class CallData {
    public:
        // Take in the "service" instance (in this case representing an asynchronous
        // server) and the completion queue "cq" used for asynchronous communication
        // with the gRPC runtime.
        CallData(elog_grpc::ELogService::AsyncService* service, grpc::ServerCompletionQueue* cq)
            : m_service(service), m_cq(cq), m_responder(&m_serverContext), m_callState(CS_CREATE) {
            // Invoke the serving logic right away.
            Proceed();
        }

        void Proceed() {
            if (m_callState == CS_CREATE) {
                // As part of the initial CREATE state, we *request* that the system
                // start processing SayHello requests. In this request, "this" acts are
                // the tag uniquely identifying the request (so that different CallData
                // instances can serve different requests concurrently), in this case
                // the memory address of this CallData instance.
                m_service->RequestSendLogRecord(&m_serverContext, &m_logRecordMsg, &m_responder,
                                                m_cq, m_cq, this);

                // Make this instance progress to the PROCESS state.
                m_callState = CS_PROCESS;
            } else if (m_callState == CS_PROCESS) {
                // handle currently arrived request (print log record)
                handleGrpcLogRecord(&m_logRecordMsg);

                // Spawn a new CallData instance to serve new clients while we process
                // the one for this CallData. The instance will deallocate itself as
                // part of its FINISH state.
                new CallData(m_service, m_cq);

                // And we are done! Let the gRPC runtime know we've finished, using the
                // memory address of this instance as the uniquely identifying tag for
                // the event.
                m_callState = CS_FINISH;
                m_responder.Finish(m_statusMsg, grpc::Status::OK, this);
            } else {
                assert(m_callState == CS_FINISH);
                // Once in the FINISH state, deallocate ourselves (CallData).
                delete this;
            }
        }

    private:
        // The means of communication with the gRPC runtime for an asynchronous
        // server.
        elog_grpc::ELogService::AsyncService* m_service;
        // The producer-consumer queue where for asynchronous server notifications.
        grpc::ServerCompletionQueue* m_cq;
        // Context for the rpc, allowing to tweak aspects of it such as the use
        // of compression, authentication, as well as to send metadata back to the
        // client.
        grpc::ServerContext m_serverContext;

        // What we get from the client.
        elog_grpc::ELogRecordMsg m_logRecordMsg;
        // What we send back to the client.
        elog_grpc::ELogStatusMsg m_statusMsg;

        // The means to get back to the client.
        grpc::ServerAsyncResponseWriter<elog_grpc::ELogStatusMsg> m_responder;

        // Let's implement a tiny state machine with the following states.
        enum CallState { CS_CREATE, CS_PROCESS, CS_FINISH };
        CallState m_callState;  // The current serving call state.
    };
};

class TestGRPCAsyncCallbackServer final : public elog_grpc::ELogService::CallbackService {
public:
    TestGRPCAsyncCallbackServer() {}

    grpc::ServerUnaryReactor* SendLogRecord(grpc::CallbackServerContext* context,
                                            const elog_grpc::ELogRecordMsg* request,
                                            elog_grpc::ELogStatusMsg* response) override {
        class Reactor : public grpc::ServerUnaryReactor {
        public:
            Reactor(const elog_grpc::ELogRecordMsg& logRecordMsg) {
                handleGrpcLogRecord(&logRecordMsg);
                Finish(grpc::Status::OK);
            }

        private:
            void OnDone() override { delete this; }
        };
        return new Reactor(*request);
    }

    grpc::ServerReadReactor< ::elog_grpc::ELogRecordMsg>* StreamLogRecords(
        ::grpc::CallbackServerContext* context, ::elog_grpc::ELogStatusMsg* response) override {
        class StreamReactor : public grpc::ServerReadReactor<elog_grpc::ELogRecordMsg> {
        public:
            StreamReactor() { StartRead(&m_logRecord); }

            void OnReadDone(bool ok) override {
                if (ok) {
                    handleGrpcLogRecord(&m_logRecord);
                    StartRead(&m_logRecord);
                } else {
                    // all stream/batch messages read, now call Finish()
                    Finish(grpc::Status::OK);
                }
            }

            void OnDone() override {
                // all reactor callbacks have been, now we can delete reactor
                delete this;
            }

        private:
            elog_grpc::ELogRecordMsg m_logRecord;
        };
        return new StreamReactor();
    }
};
#endif

#ifdef ELOG_ENABLE_GRPC_CONNECTOR
TEST(ELogGRPC, Simple) {
    int res = testGRPCSimple();
    EXPECT_EQ(res, 0);
    elog::discardAccumulatedLogMessages();
}
TEST(ELogGRPC, Stream) {
    int res = testGRPCStream();
    EXPECT_EQ(res, 0);
}
TEST(ELogGRPC, Async) {
    int res = testGRPCAsync();
    EXPECT_EQ(res, 0);
}
TEST(ELogGRPC, AsyncCallbackUnary) {
    int res = testGRPCAsyncCallbackUnary();
    EXPECT_EQ(res, 0);
}
TEST(ELogGRPC, AsyncCallbackStream) {
    int res = testGRPCAsyncCallbackStream();
    EXPECT_EQ(res, 0);
}

template <typename ServerType>
std::thread startServiceWait(std::unique_ptr<grpc::Server>& server, ServerType& service,
                             std::unique_ptr<grpc::ServerCompletionQueue>& cq) {
    return std::thread([&server]() { server->Wait(); });
}

template <>
std::thread startServiceWait<TestGRPCAsyncServer>(
    std::unique_ptr<grpc::Server>& server, TestGRPCAsyncServer& service,
    std::unique_ptr<grpc::ServerCompletionQueue>& cq) {
    return std::thread([&service, &cq]() { service.HandleRpcs(cq.get()); });
}

// #define GRPC_OPT_HAS_PRE_INIT 0x01
#define GRPC_OPT_NEED_CQ 0x02
#define GRPC_OPT_TRACE 0x04

static int sMsgCnt = -1;

template <typename ServerType>
int testGRPCClient(const char* clientType, int opts = 0, uint32_t stMsgCount = 10,
                   uint32_t mtMsgCount = 100) {
    // setup up server
    std::string serverAddress = "0.0.0.0:5051";
    ServerType service;
    grpc::ServerBuilder builder;
    builder.AddListeningPort(serverAddress, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    std::unique_ptr<grpc::ServerCompletionQueue> cq;
    if (opts & GRPC_OPT_NEED_CQ) {
        cq = builder.AddCompletionQueue();
    }

    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    DBGPRINT(stderr, "Server listening on %s\n", serverAddress.c_str());
    std::thread t = startServiceWait(server, service, cq);

    // prepare log target URL and test name
    std::string cfg =
        "rpc://grpc?rpc_server=localhost:5051&rpc_call=dummy(${rid}, ${time}, ${level}, "
        "${msg})&grpc_max_inflight_calls=20000&flush_policy=count&flush_count=1024&"
        "grpc_client_mode=";
    cfg += clientType;
    std::string testName = std::string("gRPC (") + clientType + ")";
    std::string mtResultFileName = std::string("elog_test_grpc_") + clientType;

    // run single threaded test
    double msgPerf = 0.0f;
    double ioPerf = 0.0f;

    sGrpcMsgCount.store(0, std::memory_order_relaxed);

    if (opts & GRPC_OPT_TRACE) {
        elog::setReportLevel(elog::ELEVEL_TRACE);
    }

    runSingleThreadedTest(testName.c_str(), cfg.c_str(), msgPerf, ioPerf, TT_NORMAL, stMsgCount);
    uint32_t receivedMsgCount = (uint32_t)sGrpcMsgCount.load(std::memory_order_relaxed);
    // total: 2 pre-init + stMsgCount single-thread messages
    uint32_t totalMsg = stMsgCount;
    totalMsg += elog::getAccumulatedMessageCount();
    if (receivedMsgCount != totalMsg) {
        ELOG_ERROR(
            "%s gRPC client test failed, missing messages on server side, expected %u, got "
            "%u",
            clientType, totalMsg, receivedMsgCount);
        server->Shutdown();
        t.join();
        DBGPRINT(stderr, "%s gRPC client test FAILED\n", clientType);
        return 1;
    }

    // multi-threaded test
    sMsgCnt = mtMsgCount;
    sGrpcMsgCount.store(0, std::memory_order_relaxed);
    runMultiThreadTest(testName.c_str(), mtResultFileName.c_str(), cfg.c_str(), TT_NORMAL,
                       mtMsgCount, 1, 4);
    sMsgCnt = 0;

    server->Shutdown();
    t.join();

    receivedMsgCount = (uint32_t)sGrpcMsgCount.load(std::memory_order_relaxed);
    // total: sMsgCnt multi-thread messages + total threads
    // each test adds 2 more messages for start and end test phase
    // we run total 10 threads  in 4 phases(1 + 2 + 3 + 4)
    const uint32_t threadCount = 10;
    const uint32_t phaseCount = 4;
    const uint32_t exMsgPerPhase = 2;
    totalMsg = threadCount * mtMsgCount + exMsgPerPhase * phaseCount;
    totalMsg += elog::getAccumulatedMessageCount();
    if (receivedMsgCount != totalMsg) {
        DBGPRINT(stderr,
                 "%s gRPC client test failed, missing messages on server side, expected %u, got "
                 "%u\n",
                 clientType, totalMsg, receivedMsgCount);
        DBGPRINT(stderr, "%s gRPC client test FAILED\n", clientType);
        return 2;
    }

    DBGPRINT(stderr, "%s gRPC client test PASSED\n", clientType);
    return 0;
}

int testGRPCSimple() { return testGRPCClient<TestGRPCServer>("unary"); }

int testGRPCStream() { return testGRPCClient<TestGRPCServer>("stream"); }

int testGRPCAsync() { return testGRPCClient<TestGRPCAsyncServer>("async", GRPC_OPT_NEED_CQ); }

int testGRPCAsyncCallbackUnary() {
    return testGRPCClient<TestGRPCAsyncCallbackServer>("async_callback_unary");
}

int testGRPCAsyncCallbackStream() {
    return testGRPCClient<TestGRPCAsyncCallbackServer>("async_callback_stream");
}
#endif