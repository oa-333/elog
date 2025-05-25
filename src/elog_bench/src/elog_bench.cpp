#include <chrono>
#include <cinttypes>
#include <fstream>
#include <thread>

#include "absl/log/initialize.h"
#include "elog.grpc.pb.h"
#include "elog.pb.h"
#include "elog_system.h"

static const uint64_t MSG_COUNT = 10000;
static const uint32_t MIN_THREAD_COUNT = 1;
static const uint32_t MAX_THREAD_COUNT = 16;
static const char* DEFAULT_CFG = "file://./bench_data/elog_bench.log";

static elog::ELogTarget* initElog(const char* cfg = DEFAULT_CFG);
static void termELog();
static void testPerfPrivateLog();
static void testPerfSharedLogger();
static void testGRPC();
static void testGRPCSimple();
static void testGRPCStream();
static void testGRPCAsync();
static void testGRPCAsyncCallbackUnary();
static void testGRPCAsyncCallbackStream();
static void runSingleThreadedTest(const char* title, const char* cfg, double& msgThroughput,
                                  double& ioThroughput);
static void runMultiThreadTest(const char* title, const char* fileName, const char* cfg,
                               bool privateLogger = true, uint32_t maxThreads = MAX_THREAD_COUNT);
static void printMermaidChart(const char* name, std::vector<double>& msgThroughput,
                              std::vector<double>& byteThroughput);
static void printMarkdownTable(const char* name, std::vector<double>& msgThroughput,
                               std::vector<double>& byteThroughput);
static void writeCsvFile(const char* fileName, std::vector<double>& msgThroughput,
                         std::vector<double>& byteThroughput, std::vector<double>& accumThroughput,
                         bool privateLogger);
static void testPerfFileFlushPolicy();
static void testPerfBufferedFile();
static void testPerfDeferredFile();
static void testPerfQueuedFile();
static void testPerfQuantumFile(bool privateLogger);
static void testPerfAllSingleThread();

static void testPerfFileNeverFlushPolicy();
static void testPerfImmediateFlushPolicy();
static void testPerfCountFlushPolicy();
static void testPerfSizeFlushPolicy();
static void testPerfTimeFlushPolicy();
static void testPerfCompoundFlushPolicy();

void testPerfSTFlushImmediate(std::vector<double>& msgThroughput,
                              std::vector<double>& ioThroughput);
void testPerfSTFlushNever(std::vector<double>& msgThroughput, std::vector<double>& ioThroughput);
void testPerfSTFlushCount4096(std::vector<double>& msgThroughput,
                              std::vector<double>& ioThroughput);
void testPerfSTFlushSize1mb(std::vector<double>& msgThroughput, std::vector<double>& ioThroughput);
void testPerfSTFlushTime200ms(std::vector<double>& msgThroughput,
                              std::vector<double>& ioThroughput);
void testPerfSTBufferedFile1mb(std::vector<double>& msgThroughput,
                               std::vector<double>& ioThroughput);
void testPerfSTDeferredCount4096(std::vector<double>& msgThroughput,
                                 std::vector<double>& ioThroughput);
void testPerfSTQueuedCount4096(std::vector<double>& msgThroughput,
                               std::vector<double>& ioThroughput);
void testPerfSTQuantumCount4096(std::vector<double>& msgThroughput,
                                std::vector<double>& ioThroughput);

// TODO: check rdtsc for percentile tests

#include <grpc/grpc.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>

static std::mutex coutLock;
static void handleLogRecord(const elog_grpc::ELogGRPCRecordMsg* msg) {
    return;
    std::stringstream s;
    uint32_t fieldCount = 0;
    s << "Received log record: [";
    if (msg->has_recordid()) {
        s << "{rid = " << msg->recordid() << "}";
        ++fieldCount;
    }
    if (msg->has_timeutcmillis()) {
        if (fieldCount++ > 0) s << ", ";
        s << "utc = " << msg->timeutcmillis();
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
    std::unique_lock<std::mutex> lock(coutLock);
    std::cout << s.str() << std::endl;
}

class TestGRPCServer final : public elog_grpc::ELogGRPCService::Service {
public:
    TestGRPCServer() {}

    ::grpc::Status SendLogRecord(::grpc::ServerContext* context,
                                 const ::elog_grpc::ELogGRPCRecordMsg* request,
                                 ::elog_grpc::ELogGRPCStatus* response) final {
        handleLogRecord(request);
        return grpc::Status::OK;
    }

    ::grpc::Status StreamLogRecords(::grpc::ServerContext* context,
                                    ::grpc::ServerReader< ::elog_grpc::ELogGRPCRecordMsg>* reader,
                                    ::elog_grpc::ELogGRPCStatus* response) {
        elog_grpc::ELogGRPCRecordMsg msg;
        while (reader->Read(&msg)) {
            handleLogRecord(&msg);
        }
        return grpc::Status::OK;
    }
};

class TestGRPCAsyncServer final : public elog_grpc::ELogGRPCService::AsyncService {
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
        CallData(elog_grpc::ELogGRPCService::AsyncService* service, grpc::ServerCompletionQueue* cq)
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
                handleLogRecord(&m_logRecordMsg);

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
        elog_grpc::ELogGRPCService::AsyncService* m_service;
        // The producer-consumer queue where for asynchronous server notifications.
        grpc::ServerCompletionQueue* m_cq;
        // Context for the rpc, allowing to tweak aspects of it such as the use
        // of compression, authentication, as well as to send metadata back to the
        // client.
        grpc::ServerContext m_serverContext;

        // What we get from the client.
        elog_grpc::ELogGRPCRecordMsg m_logRecordMsg;
        // What we send back to the client.
        elog_grpc::ELogGRPCStatus m_statusMsg;

        // The means to get back to the client.
        grpc::ServerAsyncResponseWriter<elog_grpc::ELogGRPCStatus> m_responder;

        // Let's implement a tiny state machine with the following states.
        enum CallState { CS_CREATE, CS_PROCESS, CS_FINISH };
        CallState m_callState;  // The current serving call state.
    };
};

class TestGRPCAsyncCallbackServer final : public elog_grpc::ELogGRPCService::CallbackService {
public:
    TestGRPCAsyncCallbackServer() {}

    grpc::ServerUnaryReactor* SendLogRecord(grpc::CallbackServerContext* context,
                                            const elog_grpc::ELogGRPCRecordMsg* request,
                                            elog_grpc::ELogGRPCStatus* response) override {
        class Reactor : public grpc::ServerUnaryReactor {
        public:
            Reactor(const elog_grpc::ELogGRPCRecordMsg& logRecordMsg) {
                handleLogRecord(&logRecordMsg);
                Finish(grpc::Status::OK);
            }

        private:
            void OnDone() override { delete this; }
        };
        return new Reactor(*request);
    }

    grpc::ServerReadReactor< ::elog_grpc::ELogGRPCRecordMsg>* StreamLogRecords(
        ::grpc::CallbackServerContext* context, ::elog_grpc::ELogGRPCStatus* response) override {
        class StreamReactor : public grpc::ServerReadReactor<elog_grpc::ELogGRPCRecordMsg> {
        public:
            StreamReactor() { StartRead(&m_logRecord); }

            void OnReadDone(bool ok) override {
                if (ok) {
                    handleLogRecord(&m_logRecord);
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
            elog_grpc::ELogGRPCRecordMsg m_logRecord;
        };
        return new StreamReactor();
    }
};

// plots:
// file flush count values
// file flush size values
// file flush time values
// flush policies compared
// quantum, deferred Vs. best sync log

int main(int argc, char* argv[]) {
    testPerfPrivateLog();
    testPerfSharedLogger();
    // testGRPC();
    testPerfFileFlushPolicy();
    testPerfBufferedFile();
    testPerfDeferredFile();
    testPerfQueuedFile();
    testPerfQuantumFile(true);
    testPerfQuantumFile(false);
    testPerfAllSingleThread();
}

elog::ELogTarget* initElog(const char* cfg /* = DEFAULT_CFG */) {
    if (!elog::ELogSystem::initialize()) {
        fprintf(stderr, "Failed to initialize elog system\n");
        return nullptr;
    }

    elog::ELogPropertySequence props;
    std::string namedCfg = cfg;
    std::string::size_type nonSpacePos = namedCfg.find_first_not_of(" \t\r\n");
    if (nonSpacePos == std::string::npos) {
        fprintf(stderr, "Invalid log target configuration, all white space\n");
        elog::ELogSystem::terminate();
        return nullptr;
    }
    if (namedCfg[nonSpacePos] != '{') {
        if (namedCfg.find('?') != std::string::npos) {
            namedCfg += "&name=elog_bench";
        } else {
            namedCfg += "?name=elog_bench";
        }
    }
    props.push_back(elog::ELogProperty("log_target", namedCfg));
    if (!elog::ELogSystem::configureFromProperties(props)) {
        fprintf(stderr, "Failed to initialize elog system with log target config: %s\n", cfg);
        elog::ELogSystem::terminate();
        return nullptr;
    }

    elog::ELogTarget* logTarget = elog::ELogSystem::getLogTarget("elog_bench");
    if (logTarget == nullptr) {
        fprintf(stderr, "Failed to find logger by name elog_bench, aborting\n");
        elog::ELogSystem::terminate();
    }
    return logTarget;
}

void termELog() { elog::ELogSystem::terminate(); }

void testPerfPrivateLog() {
    // Private logger test
    elog::ELogTarget* logTarget = initElog();
    if (logTarget == nullptr) {
        fprintf(stderr, "Failed to init private logger test, aborting\n");
        return;
    }
    elog::ELogLogger* privateLogger = elog::ELogSystem::getPrivateLogger("");

    fprintf(stderr, "Empty private log benchmark:\n");
    uint64_t bytesStart = logTarget->getBytesWritten();
    auto start = std::chrono::high_resolution_clock::now();

    // run test
    for (uint64_t i = 0; i < MSG_COUNT; ++i) {
        ELOG_DEBUG_EX(privateLogger, "Test log %u", i);
    }

    // wait for test to end
    uint64_t writeCount = 0;
    uint64_t readCount = 0;
    while (!logTarget->isCaughtUp(writeCount, readCount));
    auto end = std::chrono::high_resolution_clock::now();
    uint64_t bytesEnd = logTarget->getBytesWritten();
    std::chrono::microseconds testTime =
        std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    // print test result
    fprintf(stderr, "Test time: %u usec\n", (unsigned)testTime.count());

    double throughput = MSG_COUNT / (double)testTime.count() * 1000000.0f;
    fprintf(stderr, "Throughput: %0.3f MSg/Sec\n", throughput);

    throughput = (bytesEnd - bytesStart) / (double)testTime.count() * 1000000.0f / 1024;
    fprintf(stderr, "Throughput: %0.3f KB/Sec\n", throughput);

    termELog();
}

void testPerfSharedLogger() {
    // Shared logger test
    elog::ELogTarget* logTarget = initElog();
    if (logTarget == nullptr) {
        fprintf(stderr, "Failed to init shared logger test, aborting\n");
        return;
    }
    elog::ELogLogger* sharedLogger = elog::ELogSystem::getSharedLogger("");

    fprintf(stderr, "Empty shared log benchmark:\n");
    uint64_t bytesStart = logTarget->getBytesWritten();
    auto start = std::chrono::high_resolution_clock::now();

    // run test
    for (uint64_t i = 0; i < MSG_COUNT; ++i) {
        ELOG_DEBUG_EX(sharedLogger, "Test log %u", i);
    }

    // wait for test to end
    uint64_t writeCount = 0;
    uint64_t readCount = 0;
    while (!logTarget->isCaughtUp(writeCount, readCount));
    auto end = std::chrono::high_resolution_clock::now();
    uint64_t bytesEnd = logTarget->getBytesWritten();
    std::chrono::microseconds testTime =
        std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    // print test result
    fprintf(stderr, "Test time: %u usec\n", (unsigned)testTime.count());

    double throughput = MSG_COUNT / (double)testTime.count() * 1000000.0f;
    fprintf(stderr, "Throughput: %0.3f MSg/Sec\n", throughput);

    throughput = (bytesEnd - bytesStart) / (double)testTime.count() * 1000000.0f / 1024;
    fprintf(stderr, "Throughput: %0.3f KB/Sec\n", throughput);

    termELog();
}

void testGRPC() {
    testGRPCSimple();
    testGRPCStream();
    testGRPCAsync();
    testGRPCAsyncCallbackUnary();
    testGRPCAsyncCallbackStream();
}

void testGRPCSimple() {
    // start gRPC server
    // absl::InitializeLog();
    std::string serverAddress = "0.0.0.0:5051";
    TestGRPCServer service;
    grpc::ServerBuilder builder;
    builder.AddListeningPort(serverAddress, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    std::cout << "Server listening on " << serverAddress << std::endl;
    std::thread t = std::thread([&server]() { server->Wait(); });

    const char* cfg =
        "rpc://grpc?rpc_server=localhost:5051&rpc_call=dummy(${rid}, ${time}, ${level}, "
        "${msg})";
    double msgPerf = 0.0f;
    double ioPerf = 0.0f;
    runSingleThreadedTest("gRPC (unary)", cfg, msgPerf, ioPerf);
    runMultiThreadTest("gRPC (unary)", "elog_bench_grpc_unary", cfg, true, 4);

    server->Shutdown();
    t.join();
}

void testGRPCStream() {
    std::string serverAddress = "0.0.0.0:5051";
    TestGRPCServer service;
    grpc::ServerBuilder builder;
    builder.AddListeningPort(serverAddress, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    std::cout << "Server listening on " << serverAddress << std::endl;
    std::thread t = std::thread([&server]() { server->Wait(); });

    const char* cfg =
        "rpc://grpc?rpc_server=localhost:5051&rpc_call=dummy(${rid}, ${time}, ${level}, "
        "${msg})&grpc_client_mode=stream";
    double msgPerf = 0.0f;
    double ioPerf = 0.0f;
    runSingleThreadedTest("gRPC (stream)", cfg, msgPerf, ioPerf);
    runMultiThreadTest("gRPC (stream)", "elog_bench_grpc_stream", cfg, true, 4);

    server->Shutdown();
    t.join();
}

void testGRPCAsync() {
    std::string serverAddress = "0.0.0.0:5051";
    TestGRPCAsyncServer service;
    grpc::ServerBuilder builder;
    builder.AddListeningPort(serverAddress, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    std::unique_ptr<grpc::ServerCompletionQueue> cq = builder.AddCompletionQueue();
    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    std::cout << "Server listening on " << serverAddress << std::endl;
    std::thread t = std::thread([&service, &cq]() { service.HandleRpcs(cq.get()); });

    const char* cfg =
        "rpc://grpc?rpc_server=localhost:5051&rpc_call=dummy(${rid}, ${time}, ${level}, "
        "${msg})&grpc_client_mode=async";
    double msgPerf = 0.0f;
    double ioPerf = 0.0f;
    runSingleThreadedTest("gRPC (async)", cfg, msgPerf, ioPerf);
    runMultiThreadTest("gRPC (async)", "elog_bench_grpc_async", cfg, true, 4);

    // test is over, order server to shut down
    server->Shutdown();
    t.join();
    cq->Shutdown();
}

void testGRPCAsyncCallbackUnary() {
    std::string serverAddress = "0.0.0.0:5051";
    TestGRPCAsyncCallbackServer service;
    grpc::ServerBuilder builder;
    builder.AddListeningPort(serverAddress, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    std::cout << "Server listening on " << serverAddress << std::endl;
    std::thread t = std::thread([&server]() { server->Wait(); });

    const char* cfg =
        "rpc://grpc?rpc_server=localhost:5051&rpc_call=dummy(${rid}, ${time}, ${level}, "
        "${msg})&grpc_client_mode=async_callback_unary";
    double msgPerf = 0.0f;
    double ioPerf = 0.0f;
    runSingleThreadedTest("gRPC (async callback unary)", cfg, msgPerf, ioPerf);
    runMultiThreadTest("gRPC (async callback unary)", "elog_bench_grpc_async_cb_unary", cfg, true,
                       4);

    // test is over, order server to shut down
    server->Shutdown();
    t.join();
}

void testGRPCAsyncCallbackStream() {
    std::string serverAddress = "0.0.0.0:5051";
    TestGRPCAsyncCallbackServer service;
    grpc::ServerBuilder builder;
    builder.AddListeningPort(serverAddress, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    std::cout << "Server listening on " << serverAddress << std::endl;
    std::thread t = std::thread([&server]() { server->Wait(); });

    const char* cfg =
        "rpc://grpc?rpc_server=localhost:5051&rpc_call=dummy(${rid}, ${time}, ${level}, "
        "${msg})&grpc_client_mode=async_callback_stream&grpc_max_inflight_calls=20000&flush_policy="
        "count&flush_count=1024";
    double msgPerf = 0.0f;
    double ioPerf = 0.0f;
    runSingleThreadedTest("gRPC (async callback stream)", cfg, msgPerf, ioPerf);
    runMultiThreadTest("gRPC (async callback stream)", "elog_bench_grpc_async_cb_stream", cfg);

    // test is over, order server to shut down
    server->Shutdown();
    t.join();
}

void runSingleThreadedTest(const char* title, const char* cfg, double& msgThroughput,
                           double& ioThroughput) {
    elog::ELogTarget* logTarget = initElog(cfg);
    if (logTarget == nullptr) {
        fprintf(stderr, "Failed to init %s test, aborting\n", title);
        return;
    }

    fprintf(stderr, "\n\nRunning %s single-thread test\n", title);
    elog::ELogLogger* logger = elog::ELogSystem::getPrivateLogger("");
    uint64_t bytesStart = logTarget->getBytesWritten();
    auto start = std::chrono::high_resolution_clock::now();
    for (uint64_t j = 0; j < MSG_COUNT; ++j) {
        ELOG_INFO_EX(logger, "Single thread Test log %u", j);
    }
    auto end0 = std::chrono::high_resolution_clock::now();
    uint64_t writeCount = 0;
    uint64_t readCount = 0;
    while (!logTarget->isCaughtUp(writeCount, readCount)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(0));
    }
    auto end = std::chrono::high_resolution_clock::now();
    uint64_t bytesEnd = logTarget->getBytesWritten();
    std::chrono::microseconds testTime0 =
        std::chrono::duration_cast<std::chrono::microseconds>(end0 - start);
    std::chrono::microseconds testTime =
        std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    // print test result
    // fprintf(stderr, "Msg Test time: %u usec\n", (unsigned)testTime0.count());
    // fprintf(stderr, "IO Test time: %u usec\n", (unsigned)testTime.count());

    msgThroughput = MSG_COUNT / (double)testTime0.count() * 1000000.0f;
    fprintf(stderr, "Throughput: %0.3f MSg/Sec\n", msgThroughput);

    ioThroughput = (bytesEnd - bytesStart) / (double)testTime.count() * 1000000.0f / 1024;
    fprintf(stderr, "Throughput: %0.3f KB/Sec\n", ioThroughput);

    termELog();
}

void runMultiThreadTest(const char* title, const char* fileName, const char* cfg,
                        bool privateLogger /* = true */,
                        uint32_t maxThreads /* = MAX_THREAD_COUNT */) {
    elog::ELogTarget* logTarget = initElog(cfg);
    if (logTarget == nullptr) {
        fprintf(stderr, "Failed to init %s test, aborting\n", title);
        return;
    }

    fprintf(stderr, "\n\nRunning %s thread test\n", title);
    std::vector<double> msgThroughput;
    std::vector<double> byteThroughput;
    std::vector<double> accumThroughput;
    elog::ELogLogger* sharedLogger =
        privateLogger ? nullptr : elog::ELogSystem::getSharedLogger("");
    for (uint32_t threadCount = MIN_THREAD_COUNT; threadCount <= maxThreads; ++threadCount) {
        // fprintf(stderr, "Running %u threads test\n", threadCount);
        ELOG_INFO("Running %u Thread Test", threadCount);
        std::vector<std::thread> threads;
        std::vector<double> resVec(threadCount, 0.0);
        auto start = std::chrono::high_resolution_clock::now();
        uint64_t bytesStart = logTarget->getBytesWritten();
        for (uint32_t i = 0; i < threadCount; ++i) {
            threads.emplace_back(std::thread([i, &resVec, sharedLogger]() {
                elog::ELogLogger* logger =
                    sharedLogger != nullptr ? sharedLogger : elog::ELogSystem::getPrivateLogger("");
                auto start = std::chrono::high_resolution_clock::now();
                for (uint64_t j = 0; j < MSG_COUNT; ++j) {
                    ELOG_INFO_EX(logger, "Thread %u Test log %u", i, j);
                }
                auto end = std::chrono::high_resolution_clock::now();
                std::chrono::microseconds testTime =
                    std::chrono::duration_cast<std::chrono::microseconds>(end - start);
                double throughput = MSG_COUNT / (double)testTime.count() * 1000000.0f;
                /*fprintf(stderr, "Test time: %u usec, msg count: %u\n",
                (unsigned)testTime.count(), (unsigned)MSG_COUNT); fprintf(stderr, "Throughput:
                %0.3f MSg/Sec\n", throughput);*/
                resVec[i] = throughput;
            }));
        }
        for (uint32_t i = 0; i < threadCount; ++i) {
            threads[i].join();
        }
        auto end0 = std::chrono::high_resolution_clock::now();
        uint64_t writeCount = 0;
        uint64_t readCount = 0;
        while (!logTarget->isCaughtUp(writeCount, readCount)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(0));
            /*fprintf(stderr, "write-pos = %" PRIu64 ", read-pos = %" PRIu64 "\n", writeCount,
                    readCount);*/
        }
        // fprintf(stderr, "write-pos = %" PRIu64 ", read-pos = %" PRIu64 "\n", writeCount,
        // readCount);
        auto end = std::chrono::high_resolution_clock::now();
        ELOG_INFO("%u Thread Test ended", threadCount);
        uint64_t bytesEnd = logTarget->getBytesWritten();
        double throughput = 0;
        for (double val : resVec) {
            throughput += val;
        }
        fprintf(stderr, "%u thread accumulated throughput: %0.2f\n", threadCount, throughput);
        accumThroughput.push_back(throughput);

        std::chrono::microseconds testTime0 =
            std::chrono::duration_cast<std::chrono::microseconds>(end0 - start);
        std::chrono::microseconds testTime =
            std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        throughput = threadCount * MSG_COUNT / (double)testTime0.count() * 1000000.0f;
        /*fprintf(stderr, "%u thread Test time: %u usec, msg count: %u\n", threadCount,
                (unsigned)testTime.count(), (unsigned)MSG_COUNT);*/
        fprintf(stderr, "%u thread Throughput: %0.3f MSg/Sec\n", threadCount, throughput);
        msgThroughput.push_back(throughput);
        throughput = (bytesEnd - bytesStart) / (double)testTime.count() * 1000000.0f / 1024;
        fprintf(stderr, "%u thread Throughput: %0.3f KB/Sec\n", threadCount, throughput);
        byteThroughput.push_back(throughput);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(5000));
    termELog();

    // printMermaidChart(title, msgThroughput, byteThroughput);
    // printMarkdownTable(title, msgThroughput, byteThroughput);
    writeCsvFile(fileName, msgThroughput, byteThroughput, accumThroughput, privateLogger);
}

void printMermaidChart(const char* title, std::vector<double>& msgThroughput,
                       std::vector<double>& byteThroughput) {
    // print message throughput chart
    fprintf(stderr,
            "```mermaid\n"
            "---\n"
            "config:\n"
            "\txyChart:\n"
            "\t\twidth: 400\n"
            "\t\theight: 400\n"
            "\t\ttitleFontSize: 14\n"
            "---\n"
            "xychart-beta\n"
            "\ttitle \"%s Msg Throughput\"\n"
            "\tx-axis \"Threads\" 1 --> 16\n"
            "\ty-axis \"Logger Throughput (Msg/Sec)\"\n"
            "\tline [",
            title);
    for (uint32_t i = 0; i < msgThroughput.size(); ++i) {
        fprintf(stderr, "%.2f", msgThroughput[i]);
        if (i + 1 < msgThroughput.size()) {
            fprintf(stderr, ", ");
        }
    }
    fprintf(stderr, "]\n```\n");

    // print byte throughput chart
    fprintf(stderr,
            "```mermaid\n"
            "---\n"
            "config:\n"
            "\txyChart:\n"
            "\t\twidth: 400\n"
            "\t\theight: 400\n"
            "\t\ttitleFontSize: 14\n"
            "---\n"
            "xychart-beta\n"
            "\ttitle \"%s I/O Throughput\"\n"
            "\tx-axis \"Threads\" 1 --> 16\n"
            "\ty-axis \"Logger Throughput (KB/Sec)\"\n"
            "\tline [",
            title);
    for (uint32_t i = 0; i < byteThroughput.size(); ++i) {
        fprintf(stderr, "%.2f", byteThroughput[i] / 1024);
        if (i + 1 < byteThroughput.size()) {
            fprintf(stderr, ", ");
        }
    }
    fprintf(stderr, "]\n```\n");
}

void printMarkdownTable(const char* title, std::vector<double>& msgThroughput,
                        std::vector<double>& byteThroughput) {
    // print in markdown tabular format
    fprintf(stderr, "| Threads | Throughput (Msg/Sec) |\n");
    fprintf(stderr, "|:---|---:|\n");
    for (uint32_t i = 0; i < msgThroughput.size(); ++i) {
        fprintf(stderr, "| %u | %.2f |\n", i + 1, msgThroughput[i]);
    }

    // print in tabular format
    fprintf(stderr, "| Threads | Throughput (KB/Sec) |\n");
    fprintf(stderr, "|:---|---:|\n");
    for (uint32_t i = 0; i < byteThroughput.size(); ++i) {
        fprintf(stderr, "| %u | %.2f |\n", i + 1, msgThroughput[i] / 1024);
    }
}

void writeCsvFile(const char* fileName, std::vector<double>& msgThroughput,
                  std::vector<double>& byteThroughput, std::vector<double>& accumThroughput,
                  bool privateLogger) {
    std::string fname =
        std::string("./bench_data/") + fileName + (privateLogger ? "_msg.csv" : "_shared_msg.csv");
    std::ofstream f(fname, std::ios_base::trunc);

    // print in CSV format for gnuplot
    for (uint32_t i = 0; i < msgThroughput.size(); ++i) {
        f << (i + 1) << ", " << std::fixed << std::setprecision(2) << msgThroughput[i] << std::endl;
    }
    f.close();

    fname =
        std::string("./bench_data/") + fileName + (privateLogger ? "_io.csv" : "_shared_io.csv");
    f.open(fname, std::ios_base::trunc);
    for (uint32_t i = 0; i < byteThroughput.size(); ++i) {
        f << (i + 1) << ", " << std::fixed << std::setprecision(2) << byteThroughput[i]
          << std::endl;
    }
    f.close();

    fname = std::string("./bench_data/") + fileName +
            (privateLogger ? "_accum_msg.csv" : "_shared_accum_msg.csv");
    f.open(fname, std::ios_base::trunc);
    for (uint32_t i = 0; i < accumThroughput.size(); ++i) {
        f << (i + 1) << ", " << std::fixed << std::setprecision(2) << accumThroughput[i]
          << std::endl;
    }
    f.close();
}

void testPerfFileFlushPolicy() {
    // flush never
    testPerfFileNeverFlushPolicy();

    // flush immediate
    testPerfImmediateFlushPolicy();

    // flush count
    testPerfCountFlushPolicy();

    // flush size
    testPerfSizeFlushPolicy();

    // flush time
    testPerfTimeFlushPolicy();

    // compound flush policy, size or count
    // testPerfCompoundFlushPolicy();
}

void testPerfBufferedFile() {
    const char* cfg =
        "file://./bench_data/"
        "elog_bench_buffered512.log?file_buffer_size=512&file_lock=yes&flush_policy=none";
    runMultiThreadTest("Buffered File (512 bytes)", "elog_bench_buffered512", cfg);

    cfg =
        "file://./bench_data/"
        "elog_bench_buffered4kb.log?file_buffer_size=4096&file_lock=yes&flush_policy=none";
    runMultiThreadTest("Buffered File (4kb)", "elog_bench_buffered4kb", cfg);

    cfg =
        "file://./bench_data/"
        "elog_bench_buffered64kb.log?file_buffer_size=65536&file_lock=yes&flush_policy=none";
    runMultiThreadTest("Buffered File (64kb)", "elog_bench_buffered64kb", cfg);

    cfg =
        "file://./bench_data/"
        "elog_bench_buffered1mb.log?file_buffer_size=1048576&file_lock=yes&flush_policy=none";
    runMultiThreadTest("Buffered File (1mb)", "elog_bench_buffered1mb", cfg);

    cfg =
        "file://./bench_data/"
        "elog_bench_buffered4mb.log?file_buffer_size=4194304&file_lock=yes&flush_policy=none";
    runMultiThreadTest("Buffered File (4mb)", "elog_bench_buffered4mb", cfg);
}

void testPerfDeferredFile() {
    /*const char* cfg =
        "file://./bench_data/"
        "elog_bench_deferred.log?file_buffer_size=4194304&file_lock=no&flush_policy=count&flush-"
        "count=4096&deferred";*/
    const char* cfg =
        "file://./bench_data/"
        "elog_bench_deferred.log?flush_policy=count&flush_count=4096&deferred";
    runMultiThreadTest("Deferred (Flush Count 4096)", "elog_bench_deferred", cfg);
}

void testPerfQueuedFile() {
    /*const char* cfg =
        "file://./bench_data/"
        "elog_bench_queued.log?file_buffer_size=4194304&file_lock=no&flush_policy=count&flush-"
        "count=4096&queue_batch_size=10000&queue_"
        "timeout_millis=200";*/
    const char* cfg =
        "file://./bench_data/"
        "elog_bench_queued.log?flush_policy=count&flush_count=4096&queue_batch_size=10000&"
        "queue_"
        "timeout_millis=200";
    runMultiThreadTest("Queued 4096 + 200ms (Flush Count 4096)", "elog_bench_queued", cfg);
}

void testPerfQuantumFile(bool privateLogger) {
    /*const char* cfg =
        "file://./bench_data/"
        "elog_bench_quantum.log?file_buffer_size=4194304&file_lock=no&flush_policy=count&flush-"
        "count=4096&quantum_buffer_size=2000000";*/
    const char* cfg =
        "file://./bench_data/"
        "elog_bench_quantum.log?flush_policy=count&flush_count=4096&quantum_buffer_size="
        "2000000";
    runMultiThreadTest("Quantum 200000 (Flush Count 4096)", "elog_bench_quantum", cfg,
                       privateLogger);
}

void testPerfAllSingleThread() {
    std::vector<double> msgThroughput;
    std::vector<double> ioThroughput;

    testPerfSTFlushImmediate(msgThroughput, ioThroughput);
    testPerfSTFlushNever(msgThroughput, ioThroughput);
    testPerfSTFlushCount4096(msgThroughput, ioThroughput);
    testPerfSTFlushSize1mb(msgThroughput, ioThroughput);
    testPerfSTFlushTime200ms(msgThroughput, ioThroughput);
    testPerfSTBufferedFile1mb(msgThroughput, ioThroughput);
    testPerfSTDeferredCount4096(msgThroughput, ioThroughput);
    testPerfSTQueuedCount4096(msgThroughput, ioThroughput);
    testPerfSTQuantumCount4096(msgThroughput, ioThroughput);

    // now write CSV for drawing bar chart with gnuplot
    std::ofstream f("./bench_data/st_msg.csv", std::ios_base::trunc);
    int column = 0;
    f << column << " \"Flush\\nImmediate\" " << std::fixed << std::setprecision(2)
      << msgThroughput[column++] << std::endl;
    f << column << " \"Flush\\nNever\" " << std::fixed << std::setprecision(2)
      << msgThroughput[column++] << std::endl;
    f << column << " \"Flush\\nCount=4096\" " << std::fixed << std::setprecision(2)
      << msgThroughput[column++] << std::endl;
    f << column << " \"Flush\\nSize=1MB\" " << std::fixed << std::setprecision(2)
      << msgThroughput[column++] << std::endl;
    f << column << " \"Flush\\nTime=200ms\" " << std::fixed << std::setprecision(2)
      << msgThroughput[column++] << std::endl;
    f << column << " \"Buffered\\nSize=1MB\" " << std::fixed << std::setprecision(2)
      << msgThroughput[column++] << std::endl;
    f << column << " Deferred " << std::fixed << std::setprecision(2) << msgThroughput[column++]
      << std::endl;
    f << column << " Queued " << std::fixed << std::setprecision(2) << msgThroughput[column++]
      << std::endl;
    f << column << " Quantum " << std::fixed << std::setprecision(2) << msgThroughput[column++]
      << std::endl;
    f.close();
}

void testPerfSTFlushImmediate(std::vector<double>& msgThroughput,
                              std::vector<double>& ioThroughput) {
    const char* cfg =
        "file://./bench_data/elog_bench_flush_immediate_st.log?flush_policy=immediate";
    double msgPerf = 0.0f;
    double ioPerf = 0.0f;
    runSingleThreadedTest("Flush Immediate", cfg, msgPerf, ioPerf);
    msgThroughput.push_back(msgPerf);
    ioThroughput.push_back(ioPerf);
}

void testPerfSTFlushNever(std::vector<double>& msgThroughput, std::vector<double>& ioThroughput) {
    const char* cfg = "file://./bench_data/elog_bench_flush_never_st.log?flush_policy=never";
    double msgPerf = 0.0f;
    double ioPerf = 0.0f;
    runSingleThreadedTest("Flush Never", cfg, msgPerf, ioPerf);
    msgThroughput.push_back(msgPerf);
    ioThroughput.push_back(ioPerf);
}

void testPerfSTFlushCount4096(std::vector<double>& msgThroughput,
                              std::vector<double>& ioThroughput) {
    const char* cfg =
        "file://./bench_data/"
        "elog_bench_flush_count4096_st.log?flush_policy=count&flush_count=4096";
    double msgPerf = 0.0f;
    double ioPerf = 0.0f;
    runSingleThreadedTest("Flush Count=4096", cfg, msgPerf, ioPerf);
    msgThroughput.push_back(msgPerf);
    ioThroughput.push_back(ioPerf);
}

void testPerfSTFlushSize1mb(std::vector<double>& msgThroughput, std::vector<double>& ioThroughput) {
    const char* cfg =
        "file://./bench_data/"
        "elog_bench_flush_size_1mb_st.log?flush_policy=size&flush_size_bytes=1048576";
    double msgPerf = 0.0f;
    double ioPerf = 0.0f;
    runSingleThreadedTest("Flush Size=1MB", cfg, msgPerf, ioPerf);
    msgThroughput.push_back(msgPerf);
    ioThroughput.push_back(ioPerf);
}

void testPerfSTFlushTime200ms(std::vector<double>& msgThroughput,
                              std::vector<double>& ioThroughput) {
    const char* cfg =
        "file://./bench_data/"
        "elog_bench_flush_time_200ms_st.log?flush_policy=time&flush_timeout_millis=200";
    double msgPerf = 0.0f;
    double ioPerf = 0.0f;
    runSingleThreadedTest("Flush Time=200ms", cfg, msgPerf, ioPerf);
    msgThroughput.push_back(msgPerf);
    ioThroughput.push_back(ioPerf);
}

void testPerfSTBufferedFile1mb(std::vector<double>& msgThroughput,
                               std::vector<double>& ioThroughput) {
    const char* cfg =
        "file://./bench_data/"
        "elog_bench_buffered_1mb_st.log?file_buffer_size=1048576&flush_policy=none";
    double msgPerf = 0.0f;
    double ioPerf = 0.0f;
    runSingleThreadedTest("Buffered Size=1mb", cfg, msgPerf, ioPerf);
    msgThroughput.push_back(msgPerf);
    ioThroughput.push_back(ioPerf);
}

void testPerfSTDeferredCount4096(std::vector<double>& msgThroughput,
                                 std::vector<double>& ioThroughput) {
    /*const char* cfg =
        "file://./bench_data/"
        "elog_bench_deferred_st.log?file_buffer_size=4194304&file_lock=no&flush_policy=count&flush-"
        "count=4096&deferred";*/
    const char* cfg =
        "file://./bench_data/"
        "elog_bench_deferred_st.log?flush_policy=count&flush_count=4096&deferred";
    double msgPerf = 0.0f;
    double ioPerf = 0.0f;
    runSingleThreadedTest("Deferred", cfg, msgPerf, ioPerf);
    msgThroughput.push_back(msgPerf);
    ioThroughput.push_back(ioPerf);
}

void testPerfSTQueuedCount4096(std::vector<double>& msgThroughput,
                               std::vector<double>& ioThroughput) {
    /*const char* cfg =
        "file://./bench_data/"
        "elog_bench_queued_st.log?file_buffer_size=4194304&file_lock=no&flush_policy=count&flush-"
        "count=4096&queue_batch_size=10000&queue_"
        "timeout_millis=500";*/
    const char* cfg =
        "file://./bench_data/"
        "elog_bench_queued_st.log?flush_policy=count&flush_count=4096&queue_batch_size=10000&"
        "queue_"
        "timeout_millis=500";
    double msgPerf = 0.0f;
    double ioPerf = 0.0f;
    runSingleThreadedTest("Queued", cfg, msgPerf, ioPerf);
    msgThroughput.push_back(msgPerf);
    ioThroughput.push_back(ioPerf);
}

void testPerfSTQuantumCount4096(std::vector<double>& msgThroughput,
                                std::vector<double>& ioThroughput) {
    /*const char* cfg =
        "file://./bench_data/"
        "elog_bench_quantum_st.log?file_buffer_size=4194304&file_lock=no&flush_policy=count&flush-"
        "count=4096&quantum_buffer_size=2000000";*/
    /*const char* cfg =
        "file://./bench_data/"
        "elog_bench_quantum_st.log?flush_policy=size&flush_size_bytes=1048576&quantum_buffer_size="
        "2000000";*/
    /*const char* cfg =
        "file://./bench_data/"
        "elog_bench_quantum_st.log?flush_policy=count&flush_count=4096&quantum_buffer_size=2000000";*/
    const char* cfg =
        "{  scheme = async, "
        "   type = quantum, "
        "   quantum_buffer_size = 2000000, "
        "   name = elog_bench, "
        "   log_target = { "
        "       scheme = file, "
        "       path = ./bench_data/elog_bench_quantum_st.log, "
        "       flush_policy = count,"
        "       flush_count = 4096"
        "   }"
        "}";
    double msgPerf = 0.0f;
    double ioPerf = 0.0f;
    runSingleThreadedTest("Quantum", cfg, msgPerf, ioPerf);
    msgThroughput.push_back(msgPerf);
    ioThroughput.push_back(ioPerf);
}

void testPerfFileNeverFlushPolicy() {
    const char* cfg = "file://./bench_data/elog_bench_flush_never.log?flush_policy=never";
    runMultiThreadTest("File (Never Flush Policy)", "elog_bench_flush_never", cfg);
}

static void testPerfImmediateFlushPolicy() {
    const char* cfg = "file://./bench_data/elog_bench_flush_immediate.log?flush_policy=immediate";
    runMultiThreadTest("File (Immediate Flush Policy)", "elog_bench_flush_immediate", cfg);
}

static void testPerfCountFlushPolicy() {
    const char* cfg =
        "file://./bench_data/elog_bench_count64.log?flush_policy=count&flush_count=64";
    runMultiThreadTest("File (Count 64 Flush Policy)", "elog_bench_count64", cfg);

    cfg = "file://./bench_data/elog_bench_count256.log?flush_policy=count&flush_count=256";
    runMultiThreadTest("File (Count 256 Flush Policy)", "elog_bench_count256", cfg);

    cfg = "file://./bench_data/elog_bench_count512.log?flush_policy=count&flush_count=512";
    runMultiThreadTest("File (Count 512 Flush Policy)", "elog_bench_count512", cfg);

    cfg = "file://./bench_data/elog_bench_count1024.log?flush_policy=count&flush_count=1024";
    runMultiThreadTest("File (Count 1024 Flush Policy)", "elog_bench_count1024", cfg);

    cfg = "file://./bench_data/elog_bench_count4096.log?flush_policy=count&flush_count=4096";
    runMultiThreadTest("File (Count 4096 Flush Policy)", "elog_bench_count4096", cfg);
}

static void testPerfSizeFlushPolicy() {
    const char* cfg =
        "file://./bench_data/elog_bench_size64.log?flush_policy=size&flush_size_bytes=64";
    runMultiThreadTest("File (Size 64 bytes Flush Policy)", "elog_bench_size64", cfg);

    cfg = "file://./bench_data/elog_bench_size_1kb.log?flush_policy=size&flush_size_bytes=1024";
    runMultiThreadTest("File (Size 1KB Flush Policy)", "elog_bench_size_1kb", cfg);

    cfg = "file://./bench_data/elog_bench_size_4kb.log?flush_policy=size&flush_size_bytes=4096";
    runMultiThreadTest("File (Size 4KB Flush Policy)", "elog_bench_size_4kb", cfg);

    cfg = "file://./bench_data/elog_bench_size_64kb.log?flush_policy=size&flush_size_bytes=65536";
    runMultiThreadTest("File (Size 64KB Flush Policy)", "elog_bench_size_64kb", cfg);

    cfg =
        "file://./bench_data/"
        "elog_bench_size_1mb.log?flush_policy=size&flush_size_bytes=1048576";
    runMultiThreadTest("File (Size 1MB Flush Policy)", "elog_bench_size_1mb", cfg);
}

static void testPerfTimeFlushPolicy() {
    /*const char* cfg =
        "file://./bench_data/elog_bench_time_100ms.log?flush_policy=time&flush_timeout_millis=100";*/
    const char* cfg =
        "{ scheme = file, "
        "   path = ./bench_data/elog_bench_time_100ms.log, "
        "   flush_policy = time, "
        "   flush_timeout_millis = 100, "
        "   name = elog_bench"
        "}";
    runMultiThreadTest("File (Time 100 ms Flush Policy)", "elog_bench_time_100ms", cfg);

    cfg =
        "file://./bench_data/"
        "elog_bench_time_200ms?flush_policy=time&flush_timeout_millis=200";
    runMultiThreadTest("File (Time 200 ms Flush Policy)", "elog_bench_time_200ms", cfg);

    cfg =
        "file://./bench_data/"
        "elog_bench_time_500ms.log?flush_policy=time&flush_timeout_millis=500";
    runMultiThreadTest("File (Time 500 ms Flush Policy)", "elog_bench_time_500ms", cfg);

    cfg =
        "file://./bench_data/"
        "elog_bench_time_1000ms.log?flush_policy=time&flush_timeout_millis=1000";
    runMultiThreadTest("File (Time 1000 ms Flush Policy)", "elog_bench_time_1000ms", cfg);
}

static void testPerfCompoundFlushPolicy() {
    const char* cfg =
        "{ scheme = file, "
        "   path = ./bench_data/elog_bench_compound.log, "
        "   flush_policy = or, "
        "   flush_policy_args = ["
        "       { flush_policy = count, flush_count = 4096 },"
        "       { flush_policy = size, flush_size_bytes = 1024 },"
        "       { flush_policy = time, flush_timeout_millis = 200 }"
        "   ],"
        "   name = elog_bench"
        "}";
    runMultiThreadTest("File (Compound Flush Policy)", "elog_bench_compound", cfg);
}