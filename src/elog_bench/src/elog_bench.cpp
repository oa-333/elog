#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <condition_variable>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <thread>

// #define DEFAULT_SERVER_ADDR "192.168.108.111"
#define DEFAULT_SERVER_ADDR "192.168.56.102"

// include elog system first, then any possible connector
#include "elog.h"

#ifdef ELOG_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

#ifdef ELOG_ENABLE_GRPC_CONNECTOR
#include "rpc/elog_grpc_target.h"
#endif

#if defined(ELOG_MSVC) || defined(ELOG_MINGW)
#ifdef __clang__
#include <x86intrin.h>
#else
#include <intrin.h>
#endif
inline int64_t elog_rdtscp() {
    unsigned int dummy = 0;
    return __rdtscp(&dummy);
}
#else
#ifdef __clang__
#include <x86intrin.h>
#else
#include <x86gprintrin.h>
#endif
inline int64_t elog_rdtscp() {
    unsigned int dummy = 0;
    return __rdtscp(&dummy);
}
#endif

#ifdef ELOG_MINGW
// we need windows headers for MinGW
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#elif !defined(ELOG_WINDOWS)
#include <sys/syscall.h>
#include <unistd.h>
#ifdef SYS_gettid
#define gettid() syscall(SYS_gettid)
#else
#error "SYS_gettid unavailable on this platform"
#endif
#endif

inline uint32_t getCurrentThreadId() {
#ifdef ELOG_WINDOWS
    return GetCurrentThreadId();
#else
    return gettid();
#endif  // ELOG_WINDOWS
}

inline void pinThread(uint32_t coreId) {
#ifdef ELOG_WINDOWS
    // SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)(1ull << coreId));
#else
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(coreId, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#endif
}

// TODO: consider fixing measuring method as follows:
// single thread:
// run loop indefinitely, then:
// wait for 1 second for warmup
// then take counters (submit/collect/execute)
// wait for 3 seconds measure
// then take counters
// then stop test with a flag
// compute

// multi-thread
// do the same but figure out how to know all threads are running (we can check counters of each)

// so we need to fix counters management for that

static const uint64_t MT_MSG_COUNT = 10000;
static const uint64_t ST_MSG_COUNT = 1000000;
static const uint32_t MIN_THREAD_COUNT = 1;
static const uint32_t MAX_THREAD_COUNT = 16;
static const char* DEFAULT_CFG = "file:///./bench_data/elog_bench.log";
static bool sTestConns = false;
static bool sTestException = false;
static bool sTestEventLog = false;
static bool sTestRegression = false;
static bool sTestLifeSign = false;
static std::string sServerAddr = DEFAULT_SERVER_ADDR;
static bool sTestColors = false;
static int sMsgCnt = -1;
static int sMinThreadCnt = -1;
static int sMaxThreadCnt = -1;

enum CompressMode { CM_YES, CM_NO, CM_BOTH };
enum SyncMode { SM_SYNC, SM_ASYNC, SM_BOTH };

static bool parseCompressMode(const char* str, CompressMode& mode) {
    if (strcmp(str, "yes") == 0) {
        mode = CM_YES;
    } else if (strcmp(str, "no") == 0) {
        mode = CM_NO;
    } else if (strcmp(str, "both") == 0) {
        mode = CM_BOTH;
    } else {
        fprintf(stderr, "Invalid compression mode: %s\n", str);
        return false;
    }
    return true;
}

static bool parseSyncMode(const char* str, SyncMode& mode) {
    if (strcmp(str, "sync") == 0) {
        mode = SM_SYNC;
    } else if (strcmp(str, "async") == 0) {
        mode = SM_ASYNC;
    } else if (strcmp(str, "both") == 0) {
        mode = SM_BOTH;
    } else {
        fprintf(stderr, "Invalid sync mode: %s\n", str);
        return false;
    }
    return true;
}

// connection test options
static bool sTestGrpc = false;
static bool sTestNet = false;
static bool sTestNetTcp = false;
static bool sTestNetUdp = false;
static bool sTestIpc = false;
static bool sTestIpcPipe = false;
static CompressMode sTestCompressMode = CM_BOTH;
static SyncMode sTestSyncMode = SM_BOTH;
static bool sTestMySQL = false;
static bool sTestSQLite = false;
static bool sTestPostgreSQL = false;
static bool sTestRedis = false;
static bool sTestKafka = false;
static bool sTestGrafana = false;
static bool sTestSentry = false;
static bool sTestDatadog = false;
static bool sTestOtel = false;

struct StatData {
    double p50;
    double p95;
    double p99;

    StatData() : p50(0.0f), p95(0.0f), p99(0.0f) {}
};

static void getSamplePercentiles(const std::vector<double>& samples, StatData& percentile);

static elog::ELogTarget* initElog(const char* cfg = DEFAULT_CFG);
static void termELog();
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
#ifdef ELOG_ENABLE_NET
static int testTcp();
static int testUdp();
#endif
#ifdef ELOG_ENABLE_IPC
static int testPipe();
#endif
#ifdef ELOG_ENABLE_MYSQL_DB_CONNECTOR
static void testMySQL();
#endif
#ifdef ELOG_ENABLE_SQLITE_DB_CONNECTOR
static void testSQLite();
#endif
#ifdef ELOG_ENABLE_PGSQL_DB_CONNECTOR
static void testPostgreSQL();
#endif
#ifdef ELOG_ENABLE_REDIS_DB_CONNECTOR
static void testRedis();
#endif
#ifdef ELOG_ENABLE_KAFKA_MSGQ_CONNECTOR
static void testKafka();
#endif
#ifdef ELOG_ENABLE_GRAFANA_CONNECTOR
static void testGrafana();
#endif
#ifdef ELOG_ENABLE_SENTRY_CONNECTOR
static void testSentry();
#endif
#ifdef ELOG_ENABLE_DATADOG_CONNECTOR
static void testDatadog();
#endif
#ifdef ELOG_ENABLE_OTEL_CONNECTOR
static void testOtel();
#endif

static void runSingleThreadedTest(const char* title, const char* cfg, double& msgThroughput,
                                  double& ioThroughput, StatData& msgPercentile,
                                  uint32_t msgCount = ST_MSG_COUNT, bool enableTrace = false);
#ifdef ELOG_ENABLE_FMT_LIB
static void runSingleThreadedTestBinary(const char* title, const char* cfg, double& msgThroughput,
                                        double& ioThroughput, StatData& msgPercentile,
                                        uint32_t msgCount = ST_MSG_COUNT, bool enableTrace = false);
static void runSingleThreadedTestBinaryCached(const char* title, const char* cfg,
                                              double& msgThroughput, double& ioThroughput,
                                              StatData& msgPercentile,
                                              uint32_t msgCount = ST_MSG_COUNT,
                                              bool enableTrace = false);
static void runSingleThreadedTestBinaryPreCached(const char* title, const char* cfg,
                                                 double& msgThroughput, double& ioThroughput,
                                                 StatData& msgPercentile,
                                                 uint32_t msgCount = ST_MSG_COUNT,
                                                 bool enableTrace = false);
#endif
static void runMultiThreadTest(const char* title, const char* fileName, const char* cfg,
                               bool privateLogger = true, uint32_t minThreads = MIN_THREAD_COUNT,
                               uint32_t maxThreads = MAX_THREAD_COUNT, bool enableTrace = false);
#ifdef ELOG_ENABLE_FMT_LIB
static void runMultiThreadTestBinary(const char* title, const char* fileName, const char* cfg,
                                     bool privateLogger = true,
                                     uint32_t minThreads = MIN_THREAD_COUNT,
                                     uint32_t maxThreads = MAX_THREAD_COUNT,
                                     bool enableTrace = false);
static void runMultiThreadTestBinaryCached(const char* title, const char* fileName, const char* cfg,
                                           bool privateLogger = true,
                                           uint32_t minThreads = MIN_THREAD_COUNT,
                                           uint32_t maxThreads = MAX_THREAD_COUNT,
                                           bool enableTrace = false);
static void runMultiThreadTestBinaryPreCached(const char* title, const char* fileName,
                                              const char* cfg, bool privateLogger = true,
                                              uint32_t minThreads = MIN_THREAD_COUNT,
                                              uint32_t maxThreads = MAX_THREAD_COUNT,
                                              bool enableTrace = false);
#endif
static void printMermaidChart(const char* name, std::vector<double>& msgThroughput,
                              std::vector<double>& byteThroughput);
static void printMarkdownTable(const char* name, std::vector<double>& msgThroughput,
                               std::vector<double>& byteThroughput);
static void writeCsvFile(const char* fileName, std::vector<double>& msgThroughput,
                         std::vector<double>& byteThroughput, std::vector<double>& accumThroughput,
                         bool privateLogger);
static void testPerfFileFlushPolicy();
static void testPerfBufferedFile();
static void testPerfSegmentedFile();
static void testPerfRotatingFile();
static void testPerfDeferredFile();
static void testPerfQueuedFile();
static void testPerfQuantumFile(bool privateLogger);
static void testPerfMultiQuantumFile();
#ifdef ELOG_ENABLE_FMT_LIB
static void testPerfQuantumFileBinary();
static void testPerfQuantumFileBinaryCached();
static void testPerfQuantumFileBinaryPreCached();
static void testPerfMultiQuantumFileBinary();
static void testPerfMultiQuantumFileBinaryCached();
static void testPerfMultiQuantumFileBinaryPreCached();
#endif
static void testPerfAllSingleThread();

static void testPerfImmediateFlushPolicy();
static void testPerfFileNeverFlushPolicy();
static void testPerfGroupFlushPolicy();
static void testPerfCountFlushPolicy();
static void testPerfSizeFlushPolicy();
static void testPerfTimeFlushPolicy();
static void testPerfCompoundFlushPolicy();

static void testPerfSTFlushImmediate(std::vector<double>& msgThroughput,
                                     std::vector<double>& ioThroughput, std::vector<double>& msgp50,
                                     std::vector<double>& msgp95, std::vector<double>& msgp99);
static void testPerfSTFlushNever(std::vector<double>& msgThroughput,
                                 std::vector<double>& ioThroughput, std::vector<double>& msgp50,
                                 std::vector<double>& msgp95, std::vector<double>& msgp99);
void testPerfSTFlushGroup(std::vector<double>& msgThroughput, std::vector<double>& ioThroughput,
                          std::vector<double>& msgp50, std::vector<double>& msgp95,
                          std::vector<double>& msgp99);
static void testPerfSTFlushCount4096(std::vector<double>& msgThroughput,
                                     std::vector<double>& ioThroughput, std::vector<double>& msgp50,
                                     std::vector<double>& msgp95, std::vector<double>& msgp99);
static void testPerfSTFlushSize1mb(std::vector<double>& msgThroughput,
                                   std::vector<double>& ioThroughput, std::vector<double>& msgp50,
                                   std::vector<double>& msgp95, std::vector<double>& msgp99);
static void testPerfSTFlushTime200ms(std::vector<double>& msgThroughput,
                                     std::vector<double>& ioThroughput, std::vector<double>& msgp50,
                                     std::vector<double>& msgp95, std::vector<double>& msgp99);
static void testPerfSTBufferedFile1mb(std::vector<double>& msgThroughput,
                                      std::vector<double>& ioThroughput,
                                      std::vector<double>& msgp50, std::vector<double>& msgp95,
                                      std::vector<double>& msgp99);
static void testPerfSTSegmentedFile1mb(std::vector<double>& msgThroughput,
                                       std::vector<double>& ioThroughput,
                                       std::vector<double>& msgp50, std::vector<double>& msgp95,
                                       std::vector<double>& msgp99);
static void testPerfSTRotatingFile1mb(std::vector<double>& msgThroughput,
                                      std::vector<double>& ioThroughput,
                                      std::vector<double>& msgp50, std::vector<double>& msgp95,
                                      std::vector<double>& msgp99);
static void testPerfSTDeferredCount4096(std::vector<double>& msgThroughput,
                                        std::vector<double>& ioThroughput,
                                        std::vector<double>& msgp50, std::vector<double>& msgp95,
                                        std::vector<double>& msgp99);
static void testPerfSTQueuedCount4096(std::vector<double>& msgThroughput,
                                      std::vector<double>& ioThroughput,
                                      std::vector<double>& msgp50, std::vector<double>& msgp95,
                                      std::vector<double>& msgp99);
static void testPerfSTQuantumCount4096(std::vector<double>& msgThroughput,
                                       std::vector<double>& ioThroughput,
                                       std::vector<double>& msgp50, std::vector<double>& msgp95,
                                       std::vector<double>& msgp99);
#ifdef ELOG_ENABLE_FMT_LIB
static void testPerfSTQuantumBinary(std::vector<double>& msgThroughput,
                                    std::vector<double>& ioThroughput, std::vector<double>& msgp50,
                                    std::vector<double>& msgp95, std::vector<double>& msgp99);
static void testPerfSTQuantumBinaryCached(std::vector<double>& msgThroughput,
                                          std::vector<double>& ioThroughput,
                                          std::vector<double>& msgp50, std::vector<double>& msgp95,
                                          std::vector<double>& msgp99);
static void testPerfSTQuantumBinaryPreCached(std::vector<double>& msgThroughput,
                                             std::vector<double>& ioThroughput,
                                             std::vector<double>& msgp50,
                                             std::vector<double>& msgp95,
                                             std::vector<double>& msgp99);
static void testPerfBinaryAcceleration();
#endif

// TODO: check rdtsc for percentile tests
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
    TestServer(const char* name, commutil::ByteOrder byteOrder)
        : elog::ELogMsgServer(name, byteOrder), m_dataServer(nullptr) {}
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
        : TestServer("TCP", commutil::ByteOrder::NETWORK_ORDER), m_tcpServer(iface, port, 5, 10) {
        m_dataServer = &m_tcpServer;
    }
    ~TestTcpServer() override {}

private:
    commutil::TcpServer m_tcpServer;
};

class TestUdpServer : public TestServer {
public:
    TestUdpServer(const char* iface, int port)
        : TestServer("UDP", commutil::ByteOrder::NETWORK_ORDER), m_udpServer(iface, port, 60) {
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
    TestPipeServer(const char* pipeName)
        : TestServer("Pipe", commutil::ByteOrder::HOST_ORDER), m_pipeServer(pipeName, 5, 10) {
        m_dataServer = &m_pipeServer;
    }
    ~TestPipeServer() override {}

private:
    commutil::PipeServer m_pipeServer;
};
#endif
#endif

// plots:
// file flush count values
// file flush size values
// file flush time values
// flush policies compared
// quantum, deferred Vs. best sync log

static int testConnectors() {
    int res = 0;
#ifdef ELOG_ENABLE_GRPC_CONNECTOR
    if (sTestGrpc) {
        res = testGRPC();
        if (res != 0) {
            return res;
        }
    }
#endif
#ifdef ELOG_ENABLE_NET
    if (sTestNet || sTestNetTcp) {
        res = testTcp();
        if (res != 0) {
            return res;
        }
    }
    if (sTestNet || sTestNetUdp) {
        res = testUdp();
        if (res != 0) {
            return res;
        }
    }
#endif
#ifdef ELOG_ENABLE_IPC
    if (sTestIpc || sTestIpcPipe) {
        res = testPipe();
        if (res != 0) {
            return res;
        }
    }
#endif
#ifdef ELOG_ENABLE_MYSQL_DB_CONNECTOR
    if (sTestMySQL) {
        testMySQL();
    }
#endif
#ifdef ELOG_ENABLE_SQLITE_DB_CONNECTOR
    if (sTestSQLite) {
        testSQLite();
    }
#endif
#ifdef ELOG_ENABLE_PGSQL_DB_CONNECTOR
    if (sTestPostgreSQL) {
        testPostgreSQL();
    }
#endif
#ifdef ELOG_ENABLE_REDIS_DB_CONNECTOR
    if (sTestRedis) {
        testRedis();
    }
#endif
#ifdef ELOG_ENABLE_KAFKA_MSGQ_CONNECTOR
    if (sTestKafka) {
        testKafka();
    }
#endif
#ifdef ELOG_ENABLE_GRAFANA_CONNECTOR
    if (sTestGrafana) {
        testGrafana();
    }
#endif
#ifdef ELOG_ENABLE_SENTRY_CONNECTOR
    if (sTestSentry) {
        testSentry();
    }
#endif
#ifdef ELOG_ENABLE_DATADOG_CONNECTOR
    if (sTestDatadog) {
        testDatadog();
    }
#endif
#ifdef ELOG_ENABLE_OTEL_CONNECTOR
    if (sTestOtel) {
        testOtel();
    }
#endif
    return 0;
}
static int testColors();
static int testException();
static int testEventLog();
static int testRegression();
static int testLifeSign();

static bool sTestPerfAll = true;
static bool sTestPerfIdleLog = false;
static bool sTestPerfFileFlush = false;
static bool sTestPerfBufferedFile = false;
static bool sTestPerfSegmentedFile = false;
static bool sTestPerfRotatingFile = false;
static bool sTestPerfDeferredFile = false;
static bool sTestPerfQueuedFile = false;
static bool sTestPerfQuantumPrivateFile = false;
static bool sTestPerfQuantumSharedFile = false;
static bool sTestPerfMultiQuantumFile = false;
#ifdef ELOG_ENABLE_FMT_LIB
static bool sTestPerfQuantumBinaryFile = false;
static bool sTestPerfQuantumBinaryCachedFile = false;
static bool sTestPerfQuantumBinaryPreCachedFile = false;
static bool sTestPerfMultiQuantumBinaryFile = false;
static bool sTestPerfMultiQuantumBinaryCachedFile = false;
static bool sTestPerfMultiQuantumBinaryPreCachedFile = false;
#endif
static bool sTestSingleThread = false;

static bool sTestFileAll = true;
static bool sTestFileNever = false;
static bool sTestFileImmediate = false;
static bool sTestFileGroup = false;
static bool sTestFileCount = false;
static bool sTestFileSize = false;
static bool sTestFileTime = false;

static bool sTestSingleAll = true;
static bool sTestSingleThreadFlushImmediate = false;
static bool sTestSingleThreadFlushNever = false;
static bool sTestSingleThreadFlushGroup = false;
static bool sTestSingleThreadFlushCount = false;
static bool sTestSingleThreadFlushSize = false;
static bool sTestSingleThreadFlushTime = false;
static bool sTestSingleThreadBuffered = false;
static bool sTestSingleThreadSegmented = false;
static bool sTestSingleThreadRotating = false;
static bool sTestSingleThreadDeferred = false;
static bool sTestSingleThreadQueued = false;
static bool sTestSingleThreadQuantum = false;
#ifdef ELOG_ENABLE_FMT_LIB
static bool sTestSingleThreadQuantumBinary = false;
static bool sTestSingleThreadQuantumBinaryCached = false;
static bool sTestSingleThreadQuantumBinaryPreCached = false;
static bool sTestPerfBinaryAcceleration = false;
#endif

static int sGroupSize = 0;
static int sGroupTimeoutMicros = 0;

static bool getPerfParam(const char* param) {
    if (strcmp(param, "idle") == 0) {
        sTestPerfIdleLog = true;
    } else if (strcmp(param, "file") == 0) {
        sTestPerfFileFlush = true;
    } else if (strcmp(param, "buffered") == 0) {
        sTestPerfBufferedFile = true;
    } else if (strcmp(param, "segmented") == 0) {
        sTestPerfSegmentedFile = true;
    } else if (strcmp(param, "rotating") == 0) {
        sTestPerfRotatingFile = true;
    } else if (strcmp(param, "deferred") == 0) {
        sTestPerfDeferredFile = true;
    } else if (strcmp(param, "queued") == 0) {
        sTestPerfQueuedFile = true;
    } else if (strcmp(param, "quantum-private") == 0) {
        sTestPerfQuantumPrivateFile = true;
    } else if (strcmp(param, "quantum-shared") == 0) {
        sTestPerfQuantumSharedFile = true;
    } else if (strcmp(param, "quantum-bin") == 0) {
#ifdef ELOG_ENABLE_FMT_LIB
        sTestPerfQuantumBinaryFile = true;
#else
        fprintf(stderr, "Invalid option quantum-bin, must compile with ELOG_ENABLE_FMT_LIB=ON\n");
        return false;
#endif
    } else if (strcmp(param, "quantum-bin-cache") == 0) {
#ifdef ELOG_ENABLE_FMT_LIB
        sTestPerfQuantumBinaryCachedFile = true;
#else
        fprintf(stderr,
                "Invalid option quantum-bin-cache, must compile with ELOG_ENABLE_FMT_LIB=ON\n");
        return false;
#endif
    } else if (strcmp(param, "quantum-bin-pre-cache") == 0) {
#ifdef ELOG_ENABLE_FMT_LIB
        sTestPerfQuantumBinaryPreCachedFile = true;
#else
        fprintf(stderr,
                "Invalid option quantum-bin=pre-cache, must compile with ELOG_ENABLE_FMT_LIB=ON\n");
        return false;
#endif
    } else if (strcmp(param, "multi-quantum") == 0) {
        sTestPerfMultiQuantumFile = true;
    } else if (strcmp(param, "multi-quantum-bin") == 0) {
#ifdef ELOG_ENABLE_FMT_LIB
        sTestPerfMultiQuantumBinaryFile = true;
#else
        fprintf(stderr,
                "Invalid option multi0quantum-bin, must compile with ELOG_ENABLE_FMT_LIB=ON\n");
        return false;
#endif
    } else if (strcmp(param, "multi-quantum-bin-cache") == 0) {
#ifdef ELOG_ENABLE_FMT_LIB
        sTestPerfMultiQuantumBinaryCachedFile = true;
#else
        fprintf(
            stderr,
            "Invalid option multi-quantum-bin-cache, must compile with ELOG_ENABLE_FMT_LIB=ON\n");
        return false;
#endif
    } else if (strcmp(param, "multi-quantum-bin-pre-cache") == 0) {
#ifdef ELOG_ENABLE_FMT_LIB
        sTestPerfMultiQuantumBinaryPreCachedFile = true;
#else
        fprintf(stderr,
                "Invalid option multi-quantum-bin=pre-cache, must compile with "
                "ELOG_ENABLE_FMT_LIB=ON\n");
        return false;
#endif
    } else if (strcmp(param, "multi-thread") == 0) {
        sTestPerfDeferredFile = true;
        sTestPerfQueuedFile = true;
        sTestPerfQuantumPrivateFile = true;
        sTestPerfQuantumSharedFile = true;
#ifdef ELOG_ENABLE_FMT_LIB
        sTestPerfQuantumBinaryFile = true;
        sTestPerfQuantumBinaryCachedFile = true;
        sTestPerfQuantumBinaryPreCachedFile = true;
#endif
        sTestPerfMultiQuantumFile = true;
    } else if (strcmp(param, "single-thread") == 0) {
        sTestSingleThread = true;
#ifdef ELOG_ENABLE_FMT_LIB
        // sTestPerfBinaryAcceleration = true;
#endif
    } else {
        return false;
    }
    sTestPerfAll = false;
    return true;
}

static bool getFileParam(const char* param) {
    if (strcmp(param, "flush-immediate") == 0) {
        sTestFileImmediate = true;
    } else if (strcmp(param, "flush-never") == 0) {
        sTestFileNever = true;
    } else if (strcmp(param, "flush-group") == 0) {
        sTestFileGroup = true;
    } else if (strcmp(param, "flush-count") == 0) {
        sTestFileCount = true;
    } else if (strcmp(param, "flush-size") == 0) {
        sTestFileSize = true;
    } else if (strcmp(param, "flush-time") == 0) {
        sTestFileTime = true;
    } else {
        return false;
    }
    sTestFileAll = false;
    // sTestPerfAll = false;
    return true;
}

static bool getSingleParam(const char* param) {
    if (strcmp(param, "flush-immediate") == 0) {
        sTestSingleThreadFlushImmediate = true;
    } else if (strcmp(param, "flush-never") == 0) {
        sTestSingleThreadFlushNever = true;
    } else if (strcmp(param, "flush-group") == 0) {
        sTestSingleThreadFlushGroup = true;
    } else if (strcmp(param, "flush-count") == 0) {
        sTestSingleThreadFlushCount = true;
    } else if (strcmp(param, "flush-size") == 0) {
        sTestSingleThreadFlushSize = true;
    } else if (strcmp(param, "flush-time") == 0) {
        sTestSingleThreadFlushTime = true;
    } else if (strcmp(param, "buffered") == 0) {
        sTestSingleThreadBuffered = true;
    } else if (strcmp(param, "segmented") == 0) {
        sTestSingleThreadSegmented = true;
    } else if (strcmp(param, "rotating") == 0) {
        sTestSingleThreadRotating = true;
    } else if (strcmp(param, "deferred") == 0) {
        sTestSingleThreadDeferred = true;
    } else if (strcmp(param, "queued") == 0) {
        sTestSingleThreadQueued = true;
    } else if (strcmp(param, "quantum") == 0) {
        sTestSingleThreadQuantum = true;
    } else if (strcmp(param, "quantum-bin") == 0) {
#ifdef ELOG_ENABLE_FMT_LIB
        sTestSingleThreadQuantumBinary = true;
#else
        fprintf(stderr, "Invalid option quantum-bin, must compile with ELOG_ENABLE_FMT_LIB=ON\n");
        return false;
#endif
    } else if (strcmp(param, "quantum-bin-cache") == 0) {
#ifdef ELOG_ENABLE_FMT_LIB
        sTestSingleThreadQuantumBinaryCached = true;
#else
        fprintf(stderr,
                "Invalid option quantum-bin-cache, must compile with ELOG_ENABLE_FMT_LIB=ON\n");
        return false;
#endif
    } else if (strcmp(param, "quantum-bin-pre-cache") == 0) {
#ifdef ELOG_ENABLE_FMT_LIB
        sTestSingleThreadQuantumBinaryPreCached = true;
#else
        fprintf(stderr,
                "Invalid option quantum-bin-pre-cache, must compile with ELOG_ENABLE_FMT_LIB=ON\n");
        return false;
#endif
    } else if (strcmp(param, "bin-accel") == 0) {
#ifdef ELOG_ENABLE_FMT_LIB
        sTestPerfBinaryAcceleration = true;
#else
        fprintf(stderr, "Invalid option bin-accel, must compile with ELOG_ENABLE_FMT_LIB=ON\n");
        return false;
#endif
    } else {
        return false;
    }
    sTestSingleAll = false;
    return true;
}

static bool getConnParam(int argc, char* argv[]) {
    int i = 2;
    while (i < argc) {
        if (strcmp(argv[i], "--server-addr") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Missing server address after argument --server-addr\n");
                return false;
            }
            sServerAddr = argv[i];
        } else if (strcmp(argv[i], "--grpc") == 0) {
            sTestGrpc = true;
        } else if (strcmp(argv[i], "--net") == 0) {
            sTestNet = true;
        } else if (strcmp(argv[i], "--ipc") == 0) {
            sTestIpc = true;
        } else if (strcmp(argv[i], "--tcp") == 0) {
            sTestNetTcp = true;
        } else if (strcmp(argv[i], "--udp") == 0) {
            sTestNetUdp = true;
        } else if (strcmp(argv[i], "--pipe") == 0) {
            sTestIpcPipe = true;
        } else if (strcmp(argv[i], "--compress") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Missing argument after --compress (required, yes/no/both)\n");
                return false;
            }
            if (!parseCompressMode(argv[i], sTestCompressMode)) {
                return false;
            }
        } else if (strcmp(argv[i], "--sync-mode") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Missing argument after --sync-mode (required, sync/async/both)\n");
                return false;
            }
            if (!parseSyncMode(argv[i], sTestSyncMode)) {
                return false;
            }
        } else if (strcmp(argv[i], "--mysql") == 0) {
            sTestMySQL = true;
        } else if (strcmp(argv[i], "--sqlite") == 0) {
            sTestSQLite = true;
        } else if (strcmp(argv[i], "--postgresql") == 0) {
            sTestPostgreSQL = true;
        } else if (strcmp(argv[i], "--redis") == 0) {
            sTestRedis = true;
        } else if (strcmp(argv[i], "--kafka") == 0) {
            sTestKafka = true;
        } else if (strcmp(argv[i], "--grafana") == 0) {
            sTestGrafana = true;
        } else if (strcmp(argv[i], "--sentry") == 0) {
            sTestSentry = true;
        } else if (strcmp(argv[i], "--datadog") == 0) {
            sTestDatadog = true;
        } else if (strcmp(argv[i], "--otel") == 0) {
            sTestOtel = true;
        } else {
            fprintf(stderr, "Invalid --test-conn option: %s\n", argv[i]);
            return false;
        }
        ++i;
    }

    return true;
}

static bool parseIntParam(char* valueStr, int& value, const char* paramName) {
    std::size_t pos = 0;
    try {
        value = std::stol(valueStr, &pos);
    } catch (std::exception& e) {
        fprintf(stderr, "Invalid %s integer value '%s': %s", paramName, valueStr, e.what());
        return false;
    }
    if (pos != strlen(valueStr)) {
        fprintf(stderr, "Excess characters at %s value '%s'", paramName, valueStr);
        return false;
    }
    return true;
}

static bool parseArgs(int argc, char* argv[]) {
    if (argc == 1) {
        // run all performance tests
        sTestPerfAll = true;
        return true;
    }
    if (argc >= 2) {
        if (strcmp(argv[1], "--test-conn") == 0) {
            sTestConns = true;
            return getConnParam(argc, argv);
        } else if (strcmp(argv[1], "--test-colors") == 0) {
            sTestColors = true;
            return true;
        } else if (strcmp(argv[1], "--test-exception") == 0) {
            sTestException = true;
            return true;
        } else if (strcmp(argv[1], "--test-eventlog") == 0) {
#ifdef ELOG_WINDOWS
            sTestEventLog = true;
            return true;
#else
            fprintf(stderr, "Invalid option, --test-eventlog supported only on Windows/MinGW\n");
            return false;
#endif
        } else if (strcmp(argv[1], "--test-regression") == 0) {
            sTestRegression = true;
            return true;
        } else if (strcmp(argv[1], "--test-life-sign") == 0) {
#ifdef ELOG_ENABLE_LIFE_SIGN
            sTestLifeSign = true;
            return true;
#else
            fprintf(stderr, "Cannot test life-sign, must compile with ELOG_ENABLE_LIFE_SIGN\n");
            return false;
#endif
        }
    }

    // otherwise we expect the following format:
    // --perf idle|file|buffered|deferred|queued|quantum-private|quantum-shared|single-thread
    // this may repeat several times (override previous options)
    // for single thread test we can expect another optional parameter as follows:
    // --single
    // flush-immediate|flush-never|flush-count|flush-size|flush-time|buffered|deferred|queued|quantum
    // this may be repeated
    // if none specified then all single thread tests are performed
    // in the future we should also allow specifying count, size, time buffer size, queue
    // params, quantum params, and even entire log target specification
    int i = 1;
    while (i < argc) {
        if (strcmp(argv[i], "--perf") == 0) {
            ++i;
            if (i == argc) {
                fprintf(stderr, "ERROR: Missing argument for --perf\n");
                return false;
            }
            if (!getPerfParam(argv[i])) {
                return false;
            }
        } else if (strcmp(argv[i], "--single") == 0) {
            ++i;
            if (i == argc) {
                fprintf(stderr, "ERROR: Missing argument for --single\n");
                return false;
            }
            if (!getSingleParam(argv[i])) {
                return false;
            }
        } else if (strcmp(argv[i], "--file") == 0) {
            ++i;
            if (i == argc) {
                fprintf(stderr, "ERROR: Missing argument for --single\n");
                return false;
            }
            if (!getFileParam(argv[i])) {
                return false;
            }
        } else if (strcmp(argv[i], "--msg-count") == 0) {
            ++i;
            if (i == argc) {
                fprintf(stderr, "ERROR: Missing argument for --msg-count\n");
                return false;
            }
            if (!parseIntParam(argv[i], sMsgCnt, "--msg-cnt")) {
                return false;
            }
        } else if (strcmp(argv[i], "--thread-count") == 0) {
            ++i;
            if (i == argc) {
                fprintf(stderr, "ERROR: Missing argument for --thread-count\n");
                return false;
            }
            int threadCount = -1;
            if (!parseIntParam(argv[i], threadCount, "--thread-count")) {
                return false;
            }
            sMinThreadCnt = threadCount;
            sMaxThreadCnt = threadCount;
        } else if (strcmp(argv[i], "--min-thread-count") == 0) {
            ++i;
            if (i == argc) {
                fprintf(stderr, "ERROR: Missing argument for --min-thread-count\n");
                return false;
            }
            if (!parseIntParam(argv[i], sMinThreadCnt, "--min-thread-count")) {
                return false;
            }
        } else if (strcmp(argv[i], "--max-thread-count") == 0) {
            ++i;
            if (i == argc) {
                fprintf(stderr, "ERROR: Missing argument for --max-thread-count\n");
                return false;
            }
            if (!parseIntParam(argv[i], sMaxThreadCnt, "--max-thread-count")) {
                return false;
            }
        } else if (strcmp(argv[i], "--group-size") == 0) {
            ++i;
            if (i == argc) {
                fprintf(stderr, "ERROR: Missing argument for --group-size\n");
                return false;
            }
            if (!parseIntParam(argv[i], sGroupSize, "--group-size")) {
                return false;
            }
        } else if (strcmp(argv[i], "--group-timeout-micros") == 0) {
            ++i;
            if (i == argc) {
                fprintf(stderr, "ERROR: Missing argument for --group-timeout-micros\n");
                return false;
            }
            if (!parseIntParam(argv[i], sGroupTimeoutMicros, "--group-timeout-micros")) {
                return false;
            }
        } else {
            fprintf(stderr, "ERROR: Invalid parameter '%s'\n", argv[i]);
            return false;
        }
        ++i;
    }
    return true;
}

static void printPreInitMessages() {
    // this should trigger printing of pre-init messages
    elog::ELogTargetId id = elog::addStdErrLogTarget();
    elog::removeLogTarget(id);
    // elog::discardAccumulatedLogMessages();
}

#ifdef ELOG_ENABLE_FMT_LIB
struct Coord {
    int x;
    int y;
};

template <>
struct fmt::formatter<Coord> : formatter<std::string_view> {
    // parse is inherited from formatter<string_view>.

    auto format(Coord c, format_context& ctx) const -> format_context::iterator {
        std::string s = "{";
        s += std::to_string(c.x);
        s += ",";
        s += std::to_string(c.y);
        s += "}";
        return formatter<string_view>::format(s, ctx);
    }
};

#define COORD_CODE_ID ELOG_UDT_CODE_BASE

ELOG_DECLARE_TYPE_ENCODE_DECODE_EX(Coord, COORD_CODE_ID)

ELOG_BEGIN_IMPLEMENT_TYPE_ENCODE_EX(Coord) {
    if (!buffer.appendData(value.x)) {
        return false;
    }
    if (!buffer.appendData(value.y)) {
        return false;
    }
    return true;
}
ELOG_END_IMPLEMENT_TYPE_ENCODE_EX()

ELOG_IMPLEMENT_TYPE_DECODE_EX(Coord) {
    Coord c = {};
    if (!readBuffer.read(c.x)) {
        return false;
    }
    if (!readBuffer.read(c.y)) {
        return false;
    }
    store.push_back(c);
    return true;
}
#endif

static void testFmtLibSanity() {
#ifdef ELOG_ENABLE_FMT_LIB
    // TODO: use string log target with format line containing only ${msg} so we can inspect
    // output and compare all will be printed to default log target (stderr)
    int someInt = 5;
    ELOG_FMT_INFO("This is a test message for fmtlib: {}", someInt);
    ELOG_BIN_INFO("This is a test binary message, with int {}, bool {} and string {}", (int)5, true,
                  "test string param");
    ELOG_CACHE_INFO("This is a test binary auto-cached message, with int {}, bool {} and string {}",
                    (int)5, true, "test string param");
    elog::ELogCacheEntryId msgId = elog::getOrCacheFormatMsg(
        "This is a test binary pre-cached message, with int {}, bool {} and string {}");
    ELOG_ID_INFO(msgId, (int)5, true, "test string param");

    // UDT test
    Coord c = {5, 7};
    ELOG_BIN_INFO("This is a test binary message, with UDT coord {}", c);

    // test once macro
    for (uint32_t i = 0; i < 10; ++i) {
        ELOG_ONCE_INFO("This is a test once message");
    }

    // test once thread macro
    for (uint32_t i = 0; i < 10; ++i) {
        ELOG_ONCE_THREAD_INFO("This is a test once thread message");
    }

    // test moderate macro
    for (uint32_t i = 0; i < 30; ++i) {
        ELOG_MODERATE_INFO(2, 1, elog::ELogTimeUnits::TU_SECONDS,
                           "This is a test moderate message (twice per second)");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // test every-N macro
    for (uint32_t i = 0; i < 30; ++i) {
        ELOG_EVERY_N_INFO(10, "This is a test every-N message (one in 10 messages, total 30)");
    }
#endif
}

static void testLogMacros() {
    // test once macro
    for (uint32_t i = 0; i < 10; ++i) {
        ELOG_ONCE_INFO("This is a test once message");
    }

    // test once thread macro
    for (uint32_t i = 0; i < 10; ++i) {
        ELOG_ONCE_THREAD_INFO("This is a test once thread message");
    }

    // test moderate macro
    for (uint32_t i = 0; i < 30; ++i) {
        ELOG_MODERATE_INFO(2, 1, elog::ELogTimeUnits::TU_SECONDS,
                           "This is a test moderate message (twice per second)");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // test every-N macro
    for (uint32_t i = 0; i < 30; ++i) {
        ELOG_EVERY_N_INFO(10, "This is a test every-N message (one in 10 messages, total 30)");
    }
}

static void testJson() {
    // test structured logging in JSON format

    // clang-format off
    const char* cfg = "sys://stderr?"
        "log_format={\n"
            "\t\"time\": ${time_epoch},\n"
            "\t\"level\": \"${level}\",\n"
            "\t\"thread_id\": ${tid},\n"
            "\t\"log_source\": \"${src}\",\n"
            "\t\"log_msg\": \"${msg}\"\n"
        "}";
    // clang-format on

    elog::ELogTarget* logTarget = initElog(cfg);
    if (logTarget == nullptr) {
        fprintf(stderr, "Failed to init async-thread-name test, aborting\n");
        return;
    }

    // test moderate macro
    for (uint32_t i = 0; i < 30; ++i) {
        ELOG_MODERATE_INFO(
            2, 1, elog::ELogTimeUnits::TU_SECONDS,
            "This is a test moderate message (twice per second) with JSON structured logging");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    termELog();
}

static void testReloadConfig() {
#ifdef ELOG_ENABLE_RELOAD_CONFIG
    const char* cfg =
        "sys://stderr?log_format=${time} ${level:6} [${tid:5}] [${tname}] ${src} ${msg}";

    elog::ELogTarget* logTarget = initElog(cfg);
    if (logTarget == nullptr) {
        fprintf(stderr, "Failed to init reload-config test, aborting\n");
        return;
    }

    // launch a fre threads with same log source, have them print a few times each second, then
    // after 3 seconds change log level
    elog::defineLogSource("test_source");

    fprintf(stderr, "Launching test threads\n");
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
    fprintf(stderr, "Modifying log level to WARN by STRING (messages should stop)\n");
    elog::reloadConfigStr("{ test_source.log_level=WARN }");

    // wait 1 second and set log level back to INFO
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    fprintf(stderr, "Modifying log level back to INFO (messages should reappear)\n");
    elog::reloadConfigStr("{ test_source.log_level=INFO }");

    // wait 1 second and set log level to WARN (from file)
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    fprintf(stderr, "Modifying log level to WARN by FILE (messages should stop)\n");
    std::ofstream f("./test.cfg");
    f << "{ test_source.log_level=WARN }";
    f.close();
    elog::reloadConfigFile("./test.cfg");

    // wait 1 second and set log level back to INFO
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    fprintf(stderr, "Modifying log level back to INFO (messages should reappear)\n");
    elog::reloadConfigStr("{ test_source.log_level=INFO }");

    // wait 1 second and set log level to WARN (periodic update from file)
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    fprintf(stderr, "Modifying log level to WARN by PERIODIC update (messages should stop)\n");
    f.open("./test.cfg", std::ios::out | std::ios::trunc);
    f << "{ test_source.log_level=WARN }";
    f.close();
    elog::setPeriodicReloadConfigFile("./test.cfg");
    elog::setReloadConfigPeriodMillis(100);

    // wait 1 second and set log level back to INFO (by periodic update)
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    fprintf(stderr,
            "Modifying log level back to INFO by PERIODIC update (messages should reappear)\n");
    elog::reloadConfigStr("{ test_source.log_level=INFO }");
    f.open("./test.cfg", std::ios::out | std::ios::trunc);
    f << "{ test_source.log_level=INFO }";
    f.close();

    // NEGATIVE test
    // wait 1 second and stop periodic update
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    elog::setReloadConfigPeriodMillis(0);

    // now change lgo level in file and see there is no effect
    fprintf(stderr, "Modifying log level to WARN (no effect expected, messages should continue)\n");
    f.open("./test.cfg", std::ios::out | std::ios::trunc);
    f << "{ test_source.log_level=WARN }";
    f.close();

    // wait 1 second and set log level back to INFO
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    fprintf(stderr, "Modifying log level back to INFO (messages should reappear)\n");
    elog::reloadConfigStr("{ test_source.log_level=INFO }");

    fprintf(stderr, "Finishing test\n");
    done = true;
    for (uint32_t i = 0; i < 5; ++i) {
        threads[i].join();
    }
#endif
}

int main(int argc, char* argv[]) {
    // print some messages before elog starts
    pinThread(16);
    setlocale(LC_NUMERIC, "");
    ELOG_INFO("Accumulated message 1");
    ELOG_ERROR("Accumulated message 2");
    if (!parseArgs(argc, argv)) {
        return 1;
    }

    if (!elog::initialize()) {
        fprintf(stderr, "Failed to initialize elog system\n");
        return 1;
    }
    elog::setCurrentThreadName("elog_bench_main");
    ELOG_INFO("ELog system initialized");
    printPreInitMessages();

    int res = 0;
    if (sTestConns) {
        res = testConnectors();
    } else if (sTestColors) {
        res = testColors();
    } else if (sTestException) {
        res = testException();
    } else if (sTestEventLog) {
        res = testEventLog();
    } else if (sTestRegression) {
        res = testRegression();
    } else if (sTestLifeSign) {
        res = testLifeSign();
    } else {
        fprintf(stderr, "STARTING ELOG BENCHMARK\n");

        if (sTestPerfAll || sTestPerfIdleLog) {
            testPerfPrivateLog();
            testPerfSharedLogger();
        }
        if (sTestPerfAll || sTestPerfFileFlush) {
            testPerfFileFlushPolicy();
        }
        if (sTestPerfAll || sTestPerfBufferedFile) {
            testPerfBufferedFile();
        }
        if (sTestPerfAll || sTestPerfSegmentedFile) {
            testPerfSegmentedFile();
        }
        if (sTestPerfAll || sTestPerfRotatingFile) {
            testPerfRotatingFile();
        }
        if (sTestPerfAll || sTestPerfDeferredFile) {
            testPerfDeferredFile();
        }
        if (sTestPerfAll || sTestPerfQueuedFile) {
            testPerfQueuedFile();
        }
        if (sTestPerfAll || sTestPerfQuantumPrivateFile) {
            testPerfQuantumFile(true);
        }
        if (sTestPerfAll || sTestPerfQuantumSharedFile) {
            testPerfQuantumFile(false);
        }
#ifdef ELOG_ENABLE_FMT_LIB
        if (sTestPerfAll || sTestPerfQuantumBinaryFile) {
            testPerfQuantumFileBinary();
        }
        if (sTestPerfAll || sTestPerfQuantumBinaryCachedFile) {
            testPerfQuantumFileBinaryCached();
        }
        if (sTestPerfAll || sTestPerfQuantumBinaryPreCachedFile) {
            testPerfQuantumFileBinaryPreCached();
        }
        if (sTestPerfAll || sTestPerfMultiQuantumBinaryFile) {
            testPerfMultiQuantumFileBinary();
        }
        if (sTestPerfAll || sTestPerfMultiQuantumBinaryCachedFile) {
            testPerfMultiQuantumFileBinaryCached();
        }
        if (sTestPerfAll || sTestPerfMultiQuantumBinaryPreCachedFile) {
            testPerfMultiQuantumFileBinaryPreCached();
        }
        if (sTestPerfAll || sTestPerfBinaryAcceleration) {
            testPerfBinaryAcceleration();
        }
#endif
        if (sTestPerfAll || sTestPerfMultiQuantumFile) {
            testPerfMultiQuantumFile();
        }
        if (sTestPerfAll || sTestSingleThread) {
            testPerfAllSingleThread();
        }
    }

    elog::terminate();
    return res;
}

void getSamplePercentiles(std::vector<double>& samples, StatData& percentile) {
    std::sort(samples.begin(), samples.end());
    uint32_t sampleCount = samples.size();
    percentile.p50 = samples[sampleCount / 2];
    percentile.p95 = samples[sampleCount * 95 / 100];
    percentile.p99 = samples[sampleCount * 99 / 100];
}

elog::ELogTarget* initElog(const char* cfg /* = DEFAULT_CFG */) {
    elog::setAppName("elog_bench_app");
    if (sTestException) {
        elog::addStdErrLogTarget();
    }

    elog::ELogPropertyPosSequence props;
    std::string namedCfg = cfg;
    std::string::size_type nonSpacePos = namedCfg.find_first_not_of(" \t\r\n");
    if (nonSpacePos == std::string::npos) {
        fprintf(stderr, "Invalid log target configuration, all white space\n");
        return nullptr;
    }
    bool res = false;
    if (namedCfg[nonSpacePos] != '{') {
        if (namedCfg.find("name=elog_bench") == std::string::npos) {
            if (namedCfg.find('?') != std::string::npos) {
                namedCfg += "&name=elog_bench";
            } else {
                namedCfg += "?name=elog_bench";
            }
        }
        static int confType = 0;
        if (++confType % 2 == 0) {
            fprintf(stderr, "Using configuration: log_target = %s\n", namedCfg.c_str());
            elog::ELogStringPropertyPos* prop =
                new elog::ELogStringPropertyPos(namedCfg.c_str(), 0, 0);
            props.m_sequence.push_back({"log_target", prop});
            res = elog::configureByPropsEx(props, true, true);
        } else {
            std::string cfgStr = "{ log_target = \'";
            cfgStr += namedCfg + "\'}";
            fprintf(stderr, "Using configuration: log_target = %s\n", namedCfg.c_str());
            res = elog::configureByStr(cfgStr.c_str(), true, true);
        }
    } else {
        res = elog::configureByStr(cfg, true, true);
    }
    if (!res) {
        fprintf(stderr, "Failed to initialize elog system with log target config: %s\n", cfg);
        return nullptr;
    }
    fprintf(stderr, "Configure from props OK\n");

    elog::ELogTarget* logTarget = elog::getLogTarget("elog_bench");
    if (logTarget == nullptr) {
        fprintf(stderr, "Failed to find logger by name elog_bench, aborting\n");
        return nullptr;
    }
    elog::ELogSource* logSource = elog::defineLogSource("elog_bench_logger");
    elog::ELogTargetAffinityMask mask = 0;
    ELOG_ADD_TARGET_AFFINITY_MASK(mask, logTarget->getId());
    logSource->setLogTargetAffinity(mask);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    return logTarget;
}

void termELog() {
    elog::ELogTarget* logTarget = elog::getLogTarget("elog_bench");
    if (logTarget != nullptr) {
        elog::ELogTargetId id = elog::addStdErrLogTarget();
        elog::ELogBuffer buffer;
        logTarget->statsToString(buffer);
        fputs(buffer.getRef(), stderr);
        elog::removeLogTarget(id);
    }
    elog::clearAllLogTargets();
}

inline bool isCaughtUp(elog::ELogTarget* logTarget, uint64_t targetMsgCount) {
    bool caughtUp = false;
    return logTarget->isCaughtUp(targetMsgCount, caughtUp) && caughtUp;
}

static int testAsyncThreadName() {
    const char* cfg =
        "async://quantum?quantum_buffer_size=2000000&name=elog_bench | "
        "sys://stderr?log_format=${time} ${level:6} [${tid:5}] [${tname}] ${src} ${msg}";

    elog::ELogTarget* logTarget = initElog(cfg);
    if (logTarget == nullptr) {
        fprintf(stderr, "Failed to init async-thread-name test, aborting\n");
        return 1;
    }

    ELOG_INFO("Test thread name/id, expecting elog_bench_main/%u", getCurrentThreadId());

    // wait for 1 message to be fully processed
    while (!isCaughtUp(logTarget, 1));

    std::thread t = std::thread([logTarget]() {
        elog::setCurrentThreadName("another_thread");
        ELOG_INFO("Test thread name/id, expecting another_thread/%u", getCurrentThreadId());

        uint64_t writeCount = 0;
        uint64_t readCount = 0;
        // wait for 2 messages to be fully processed
        while (!isCaughtUp(logTarget, 2));
    });

    t.join();

    termELog();
    return 0;
}

#ifdef ELOG_ENABLE_STACK_TRACE
static int testLogStackTrace() {
    const char* cfg =
        "async://quantum?quantum_buffer_size=1000&name=elog_bench | "
        "sys://stderr?log_format=${time} ${level:6} [${tid:5}] [${tname}] ${src} ${msg}&"
        "flush_policy=immediate";

    elog::ELogTarget* logTarget = initElog(cfg);
    if (logTarget == nullptr) {
        fprintf(stderr, "Failed to init async-thread-name test, aborting\n");
        return 1;
    }

    ELOG_STACK_TRACE(elog::ELEVEL_INFO, "some test title 1", 0, "Testing stack trace for thread %u",
                     getCurrentThreadId());

    ELOG_APP_STACK_TRACE(elog::ELEVEL_INFO, "some test title 2", 0,
                         "Testing app stack trace for thread %u", getCurrentThreadId());

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    termELog();
    return 0;
}
#endif

static int testRegression() {
    int res = testAsyncThreadName();
    if (res != 0) {
        return res;
    }
#ifdef ELOG_ENABLE_STACK_TRACE
    res = testLogStackTrace();
    if (res != 0) {
        return res;
    }
#endif
#ifdef ELOG_ENABLE_FMT_LIB
    testFmtLibSanity();
#endif
    testLogMacros();
    testJson();
    testReloadConfig();
    return 0;
}

#ifdef ELOG_ENABLE_LIFE_SIGN
static int testAppLifeSign(uint32_t threadCount) {
    fprintf(stderr, "Application life-sign test starting\n");

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
    fprintf(stderr, "Launching test threads\n");
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
    fprintf(stderr, "Launched all threads\n");

    // let threads work for 5 seconds and close
    std::this_thread::sleep_for(std::chrono::seconds(5));
    fprintf(stderr, "Wait ended, joining threads\n");
    done = true;
    for (auto& t : threads) {
        t.join();
    }
    fprintf(stderr, "All threads finished\n");

    if (!elog::removeLifeSignReport(elog::ELogLifeSignScope::LS_APP, elog::ELEVEL_INFO)) {
        ELOG_ERROR("Failed to remove life-sign report");
        return 1;
    }
    fprintf(stderr, "Application-level life-sign test finished\n");
    return 0;
}

static int testThreadLifeSign(uint32_t threadCount) {
    fprintf(stderr, "Thread-level life-sign test starting\n");

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
    fprintf(stderr, "Launched all threads\n");

    // let threads work for 5 seconds and close
    std::this_thread::sleep_for(std::chrono::seconds(5));
    fprintf(stderr, "Wait ended, joining threads\n");
    done = true;
    for (auto& t : threads) {
        t.join();
    }
    for (int res : threadRes) {
        if (res != 0) {
            fprintf(stderr, "Thread-level filter test failed\n");
            return res;
        }
    }
    fprintf(stderr, "Thread-level life-sign test ended, aborting\n");
    return 0;
}

static int testLogSourceLifeSign(uint32_t threadCount) {
    fprintf(stderr, "log-source life-sign test starting\n");
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
    fprintf(stderr, "Launched all threads\n");

    // let threads work for 5 seconds and close
    std::this_thread::sleep_for(std::chrono::seconds(5));
    fprintf(stderr, "Wait ended, joining threads\n");
    done = true;
    for (auto& t : threads) {
        t.join();
    }
    fprintf(stderr, "Log-source life-sign test ended\n");

    if (!elog::removeLogSourceLifeSignReport(elog::ELEVEL_INFO,
                                             elog::getDefaultLogger()->getLogSource())) {
        ELOG_ERROR("Failed to remove life-sign report for default logger");
        return 1;
    }
    return 0;
}

static int testTargetThreadLifeSign() {
    fprintf(stderr, "Target-thread life-sign test starting\n");
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
    fprintf(stderr, "Launched test thread\n");

    // let thread work for 5 seconds and close
    std::this_thread::sleep_for(std::chrono::seconds(5));
    fprintf(stderr, "Wait ended, joining thread\n");
    done = true;
    t.join();
    fprintf(stderr, "Target thread life-sign test ended\n");
    return 0;
}
#endif

static int testLifeSign() {
#ifdef ELOG_ENABLE_LIFE_SIGN
    // baseline test - no filter used, direct life sign report
    fprintf(stderr, "Running basic life-sign test\n");
    elog::ELogTarget* logTarget = initElog();
    if (logTarget == nullptr) {
        fprintf(stderr, "Failed to init life-sign test, aborting\n");
        return 1;
    }
    fprintf(stderr, "initElog() OK\n");

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

    abort();
    return 0;
#else
    return -1;
#endif
}

void testPerfPrivateLog() {
    // Private logger test
    fprintf(stderr, "Running Empty Private logger test\n");
    elog::ELogTarget* logTarget = initElog();
    if (logTarget == nullptr) {
        fprintf(stderr, "Failed to init private logger test, aborting\n");
        return;
    }
    fprintf(stderr, "initElog() OK\n");
    elog::ELogLogger* privateLogger = elog::getPrivateLogger("");
    fprintf(stderr, "private logger retrieved\n");

    fprintf(stderr, "Empty private log benchmark:\n");
    uint64_t bytesStart = logTarget->getBytesWritten();
    auto start = std::chrono::high_resolution_clock::now();

    // run test
    for (uint64_t i = 0; i < ST_MSG_COUNT; ++i) {
        ELOG_DEBUG_EX(privateLogger, "Test log %u", i);
    }

    // no need to wait for test to end, because no messages were issued
    auto end = std::chrono::high_resolution_clock::now();
    uint64_t bytesEnd = logTarget->getBytesWritten();
    std::chrono::microseconds testTime =
        std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    // print test result
    fprintf(stderr, "Test time: %u usec\n", (unsigned)testTime.count());

    double throughput = ST_MSG_COUNT / (double)testTime.count() * 1000000.0f;
    fprintf(stderr, "Throughput: %0.3f MSg/Sec\n", throughput);

    throughput = (bytesEnd - bytesStart) / (double)testTime.count() * 1000000.0f / 1024;
    fprintf(stderr, "Throughput: %0.3f KB/Sec\n", throughput);

    termELog();
}

void testPerfSharedLogger() {
    // Shared logger test
    fprintf(stderr, "Running Empty Shared logger test\n");
    elog::ELogTarget* logTarget = initElog();
    if (logTarget == nullptr) {
        fprintf(stderr, "Failed to init shared logger test, aborting\n");
        return;
    }
    elog::ELogLogger* sharedLogger = elog::getSharedLogger("");

    fprintf(stderr, "Empty shared log benchmark:\n");
    uint64_t bytesStart = logTarget->getBytesWritten();
    auto start = std::chrono::high_resolution_clock::now();

    // run test
    for (uint64_t i = 0; i < ST_MSG_COUNT; ++i) {
        ELOG_DEBUG_EX(sharedLogger, "Test log %u", i);
    }

    // no need to wait for test to end, because no messages were issued
    auto end = std::chrono::high_resolution_clock::now();
    uint64_t bytesEnd = logTarget->getBytesWritten();
    std::chrono::microseconds testTime =
        std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    // print test result
    fprintf(stderr, "Test time: %u usec\n", (unsigned)testTime.count());

    double throughput = ST_MSG_COUNT / (double)testTime.count() * 1000000.0f;
    fprintf(stderr, "Throughput: %0.3f MSg/Sec\n", throughput);

    throughput = (bytesEnd - bytesStart) / (double)testTime.count() * 1000000.0f / 1024;
    fprintf(stderr, "Throughput: %0.3f KB/Sec\n", throughput);

    termELog();
}

#ifdef ELOG_ENABLE_GRPC_CONNECTOR
int testGRPC() {
    int res = testGRPCSimple();
    if (res != 0) {
        return res;
    }
    elog::discardAccumulatedLogMessages();

    res = testGRPCStream();
    if (res != 0) {
        return res;
    }

    res = testGRPCAsync();
    if (res != 0) {
        return res;
    }

    res = testGRPCAsyncCallbackUnary();
    if (res != 0) {
        return res;
    }

    res = testGRPCAsyncCallbackStream();
    if (res != 0) {
        return res;
    }

    return 0;
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

#define GRPC_OPT_HAS_PRE_INIT 0x01
#define GRPC_OPT_NEED_CQ 0x02
#define GRPC_OPT_TRACE 0x04

template <typename ServerType>
int testGRPCClient(const char* clientType, int opts = 0, uint32_t stMsgCount = 1000,
                   uint32_t mtMsgCount = 1000) {
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
    std::cout << "Server listening on " << serverAddress << std::endl;
    std::thread t = startServiceWait(server, service, cq);

    // prepare log target URL and test name
    std::string cfg =
        "rpc://grpc?rpc_server=localhost:5051&rpc_call=dummy(${rid}, ${time}, ${level}, "
        "${msg})&grpc_max_inflight_calls=20000&flush_policy=count&flush_count=1024&"
        "grpc_client_mode=";
    cfg += clientType;
    std::string testName = std::string("gRPC (") + clientType + ")";
    std::string mtResultFileName = std::string("elog_bench_grpc_") + clientType;

    // run single threaded test
    double msgPerf = 0.0f;
    double ioPerf = 0.0f;
    StatData statData;

    sGrpcMsgCount.store(0, std::memory_order_relaxed);

    if (opts & GRPC_OPT_TRACE) {
        elog::setReportLevel(elog::ELEVEL_TRACE);
    }

    runSingleThreadedTest(testName.c_str(), cfg.c_str(), msgPerf, ioPerf, statData, stMsgCount);
    uint32_t receivedMsgCount = (uint32_t)sGrpcMsgCount.load(std::memory_order_relaxed);
    // total: 2 pre-init + stMsgCount single-thread messages
    uint32_t totalMsg = stMsgCount;
    if (opts & GRPC_OPT_HAS_PRE_INIT) {
        totalMsg += 2;
    }
    if (receivedMsgCount != totalMsg) {
        fprintf(stderr,
                "%s gRPC client test failed, missing messages on server side, expected %u, got "
                "%u\n",
                clientType, totalMsg, receivedMsgCount);
        server->Shutdown();
        t.join();
        fprintf(stderr, "%s gRPC client test FAILED\n", clientType);
        return 1;
    }

    // multi-threaded test
    sMsgCnt = mtMsgCount;
    sGrpcMsgCount.store(0, std::memory_order_relaxed);
    runMultiThreadTest(testName.c_str(), mtResultFileName.c_str(), cfg.c_str(), true, 1, 4);
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
    if (opts & GRPC_OPT_HAS_PRE_INIT) {
        totalMsg += 2;
    }
    if (receivedMsgCount != totalMsg) {
        fprintf(stderr,
                "%s gRPC client test failed, missing messages on server side, expected %u, got "
                "%u\n",
                clientType, totalMsg, receivedMsgCount);
        fprintf(stderr, "%s gRPC client test FAILED\n", clientType);
        return 2;
    }

    fprintf(stderr, "%s gRPC client test PASSED\n", clientType);
    return 0;
}

int testGRPCSimple() {
    return testGRPCClient<TestGRPCServer>("unary", GRPC_OPT_HAS_PRE_INIT, 10, 100);
}

int testGRPCStream() { return testGRPCClient<TestGRPCServer>("stream"); }

int testGRPCAsync() {
    return testGRPCClient<TestGRPCAsyncServer>("async", GRPC_OPT_NEED_CQ, 10, 100);
}

int testGRPCAsyncCallbackUnary() {
    return testGRPCClient<TestGRPCAsyncCallbackServer>("async_callback_unary", 10, 100);
}

int testGRPCAsyncCallbackStream() {
    return testGRPCClient<TestGRPCAsyncCallbackServer>("async_callback_stream");
}
#endif

#if defined(ELOG_ENABLE_NET) || defined(ELOG_ENABLE_IPC)

#define MSG_OPT_HAS_PRE_INIT 0x01
#define MSG_OPT_TRACE 0x02

int testMsgClient(TestServer& server, const char* schema, const char* serverType, const char* mode,
                  const char* address, bool compress = false, int opts = 0,
                  uint32_t stMsgCount = 1000, uint32_t mtMsgCount = 1000) {
    if (!server.initTestServer()) {
        fprintf(stderr, "Failed to initialize test server\n");
        return 1;
    }
    if (server.start() != commutil::ErrorCode::E_OK) {
        fprintf(stderr, "Failed to start test server\n");
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
    std::string mtResultFileName = std::string("elog_bench_") + mode + "_" + serverType;

    // run single threaded test
    double msgPerf = 0.0f;
    double ioPerf = 0.0f;
    StatData statData;

    sNetMsgCount.store(0, std::memory_order_relaxed);

    if (elog::hasAccumulatedLogMessages()) {
        opts |= MSG_OPT_HAS_PRE_INIT;
    }

    if (opts & MSG_OPT_TRACE) {
        elog::setReportLevel(elog::ELEVEL_TRACE);
    }

    runSingleThreadedTest(testName.c_str(), cfg.c_str(), msgPerf, ioPerf, statData, stMsgCount);
    uint32_t receivedMsgCount = (uint32_t)sNetMsgCount.load(std::memory_order_relaxed);
    // total: 2 pre-init + stMsgCount single-thread messages
    uint32_t totalMsg = stMsgCount;
    if (opts & MSG_OPT_HAS_PRE_INIT) {
        totalMsg += 2;
    }
    if (receivedMsgCount != totalMsg) {
        fprintf(stderr,
                "%s client single-thread test failed, missing messages on server side, expected "
                "%u, got %u\n",
                testName.c_str(), totalMsg, receivedMsgCount);
        server.stop();
        server.terminate();
        fprintf(stderr, "%s client test FAILED\n", testName.c_str());
        return 1;
    }

    // multi-threaded test
    sMsgCnt = mtMsgCount;
    sNetMsgCount.store(0, std::memory_order_relaxed);
    runMultiThreadTest(testName.c_str(), mtResultFileName.c_str(), cfg.c_str(), true, 1, 4);
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
    if (opts & MSG_OPT_HAS_PRE_INIT) {
        totalMsg += 2;
    }
    if (receivedMsgCount != totalMsg) {
        fprintf(stderr,
                "%s client multi-thread test failed, missing messages on server side, expected %u, "
                "got %u\n",
                testName.c_str(), totalMsg, receivedMsgCount);
        fprintf(stderr, "%s client test FAILED\n", testName.c_str());
        return 2;
    }

    if (compress) {
        fprintf(stderr, "%s client test (compressed) PASSED\n", testName.c_str());
    } else {
        fprintf(stderr, "%s client test PASSED\n", testName.c_str());
    }
    return 0;
}

#endif

#ifdef ELOG_ENABLE_NET

static int testTcpSync(bool compress);
static int testUdpSync(bool compress);
static int testTcpAsync(bool compress);
static int testUdpAsync(bool compress);

int testTcp() {
    int res = 0;
    if (sTestSyncMode == SM_SYNC || sTestSyncMode == SM_BOTH) {
        if (sTestCompressMode == CM_NO || sTestCompressMode == CM_BOTH) {
            res = testTcpSync(false);
            if (res != 0) {
                return res;
            }
        }
        elog::discardAccumulatedLogMessages();

        if (sTestCompressMode == CM_YES || sTestCompressMode == CM_BOTH) {
            res = testTcpSync(true);
            if (res != 0) {
                return res;
            }
        }
    }

    if (sTestSyncMode == SM_ASYNC || sTestSyncMode == SM_BOTH) {
        if (sTestCompressMode == CM_NO || sTestCompressMode == CM_BOTH) {
            res = testTcpAsync(false);
            if (res != 0) {
                return res;
            }
        }

        if (sTestCompressMode == CM_YES || sTestCompressMode == CM_BOTH) {
            res = testTcpAsync(true);
            if (res != 0) {
                return res;
            }
        }
    }

    return 0;
}

int testUdp() {
    int res = 0;
    if (sTestSyncMode == SM_SYNC || sTestSyncMode == SM_BOTH) {
        if (sTestCompressMode == CM_NO || sTestCompressMode == CM_BOTH) {
            res = testUdpSync(false);
            if (res != 0) {
                return res;
            }
        }

        if (sTestCompressMode == CM_YES || sTestCompressMode == CM_BOTH) {
            res = testUdpSync(true);
            if (res != 0) {
                return res;
            }
        }
    }

    if (sTestSyncMode == SM_ASYNC || sTestSyncMode == SM_BOTH) {
        if (sTestCompressMode == CM_NO || sTestCompressMode == CM_BOTH) {
            res = testUdpAsync(false);
            if (res != 0) {
                return res;
            }
        }

        if (sTestCompressMode == CM_YES || sTestCompressMode == CM_BOTH) {
            res = testUdpAsync(true);
            if (res != 0) {
                return res;
            }
        }
    }

    return 0;
}

int testTcpSync(bool compress) {
    TestTcpServer server("0.0.0.0", 5051);
    std::cout << "Server listening on port 5051" << std::endl;
    return testMsgClient(server, "net", "tcp", "sync", "127.0.0.1:5051", compress);
}

int testTcpAsync(bool compress) {
    TestTcpServer server("0.0.0.0", 5051);
    std::cout << "Server listening on port 5051" << std::endl;
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

int testPipe() {
    int res = 0;
    if (sTestSyncMode == SM_SYNC || sTestSyncMode == SM_BOTH) {
        if (sTestCompressMode == CM_NO || sTestCompressMode == CM_BOTH) {
            res = testPipeSync(false);
            if (res != 0) {
                return res;
            }
            elog::discardAccumulatedLogMessages();
        }

        if (sTestCompressMode == CM_YES || sTestCompressMode == CM_BOTH) {
            res = testPipeSync(true);
            if (res != 0) {
                return res;
            }
        }
    }

    if (sTestSyncMode == SM_ASYNC || sTestSyncMode == SM_BOTH) {
        if (sTestCompressMode == CM_NO || sTestCompressMode == CM_BOTH) {
            res = testPipeAsync(false);
            if (res != 0) {
                return res;
            }
        }

        if (sTestCompressMode == CM_YES || sTestCompressMode == CM_BOTH) {
            res = testPipeAsync(true);
            if (res != 0) {
                return res;
            }
        }
    }

    return 0;
}

int testPipeSync(bool compress) {
    TestPipeServer server("elog_test_pipe");
    std::cout << "Server listening on pipe elog_test_pipe" << std::endl;
    return testMsgClient(server, "ipc", "pipe", "sync", "elog_test_pipe", compress);
}

int testPipeAsync(bool compress) {
    TestPipeServer server("elog_test_pipe");
    std::cout << "Server listening on pipe elog_test_pipe" << std::endl;
    return testMsgClient(server, "ipc", "pipe", "async", "elog_test_pipe", compress);
}
#endif

#ifdef ELOG_ENABLE_MYSQL_DB_CONNECTOR
void testMySQL() {
    const char* cfg =
        "db://mysql?conn_string=tcp://127.0.0.1&db=test&user=root&passwd=root&"
        "insert_query=INSERT INTO log_records VALUES(${rid}, ${time}, ${level}, ${host}, "
        "${user},"
        "${prog}, ${pid}, ${tid}, ${mod}, ${src}, ${msg})&"
        "db_thread_model=conn-per-thread";
    double msgPerf = 0.0f;
    double ioPerf = 0.0f;
    StatData statData;
    runSingleThreadedTest("MySQL", cfg, msgPerf, ioPerf, statData, 10);
}
#endif

#ifdef ELOG_ENABLE_SQLITE_DB_CONNECTOR
void testSQLite() {
    const char* cfg =
        "db://sqlite?conn_string=test.db&"
        "insert_query=INSERT INTO log_records VALUES(${rid}, ${time}, ${level}, ${host}, "
        "${user},"
        "${prog}, ${pid}, ${tid}, ${mod}, ${src}, ${msg})&"
        "db_thread_model=conn-per-thread";
    double msgPerf = 0.0f;
    double ioPerf = 0.0f;
    StatData statData;
    runSingleThreadedTest("PostgreSQL", cfg, msgPerf, ioPerf, statData, 10);
}
#endif

#ifdef ELOG_ENABLE_PGSQL_DB_CONNECTOR
void testPostgreSQL() {
    std::string cfg = std::string("db://postgresql?conn_string=") + sServerAddr +
                      "&port=5432&db=mydb&user=oren&passwd=\"1234\"&"
                      "insert_query=INSERT INTO log_records VALUES(${rid}, ${time}, ${level}, "
                      "${host}, ${user},"
                      "${prog}, ${pid}, ${tid}, ${mod}, ${src}, ${msg})&"
                      "db_thread_model=conn-per-thread";
    double msgPerf = 0.0f;
    double ioPerf = 0.0f;
    StatData statData;
    runSingleThreadedTest("PostgreSQL", cfg.c_str(), msgPerf, ioPerf, statData, 10);
}
#endif

#ifdef ELOG_ENABLE_REDIS_DB_CONNECTOR
void testRedis() {
    std::string cfg =
        std::string("db://redis?conn_string=") + sServerAddr +
        ":6379&passwd=\"1234\"&"
        "insert_query=HSET log_records:${rid} time \"${time}\" level \"${level}\" "
        "host \"${host}\" user \"${user}\" prog \"${prog}\" pid \"${pid}\" tid \"${tid}\" "
        "mod \"${mod}\" src \"${src}\" msg \"${msg}\"&"
        "index_insert=SADD log_records_all ${rid};ZADD log_records_by_time ${time_epoch} ${rid}&"
        "db_thread_model=conn-per-thread";
    double msgPerf = 0.0f;
    double ioPerf = 0.0f;
    StatData statData;
    runSingleThreadedTest("Redis", cfg.c_str(), msgPerf, ioPerf, statData, 10);
}
#endif

#ifdef ELOG_ENABLE_KAFKA_MSGQ_CONNECTOR
void testKafka() {
    std::string cfg =
        std::string("msgq://kafka?kafka_bootstrap_servers=") + sServerAddr +
        ":9092&"
        "msgq_topic=log_records&"
        "kafka_flush_timeout=50millis&"
        "flush_policy=immediate&"
        "headers={rid=${rid}, time=${time}, level=${level}, host=${host}, user=${user}, "
        "prog=${prog}, pid = ${pid}, tid = ${tid}, tname = ${tname}, file = ${file}, "
        "line = ${line}, func = ${func}, mod = ${mod}, src = ${src}, msg = ${msg}}";
    double msgPerf = 0.0f;
    double ioPerf = 0.0f;
    StatData statData;
    runSingleThreadedTest("Kafka", cfg.c_str(), msgPerf, ioPerf, statData, 10);
}
#endif

#ifdef ELOG_ENABLE_GRAFANA_CONNECTOR
void testGrafana() {
    std::string cfg = std::string("mon://grafana?mode=json&loki_address=http://") + sServerAddr +
                      ":3100&labels={app: test}&flush_policy=count&flush_count=10";
    double msgPerf = 0.0f;
    double ioPerf = 0.0f;
    StatData statData;
    runSingleThreadedTest("Grafana-Loki", cfg.c_str(), msgPerf, ioPerf, statData, 100);
}
#endif

#ifdef ELOG_ENABLE_SENTRY_CONNECTOR
void testSentry() {
    // dsn comes from env
    const char* cfg =
        "mon://sentry?"
        "db_path=.sentry-native&"
        "release=native@1.0&"
        "env=staging&"
        "handler_path=vcpkg_installed\\x64-windows\\tools\\sentry-native\\crashpad_handler.exe&"
        "flush_policy=immediate&"
        "debug=true&"
        "logger_level=DEBUG&"
        "tags={log_source=${src}, module=${mod}, file=${file}, line=${line}}&"
        "stack_trace=yes&"
        "context={app=${app}, os=${os_name}, ver=${os_ver}}&"
        "context_title=Env Details";
    double msgPerf = 0.0f;
    double ioPerf = 0.0f;
    StatData statData;
    runSingleThreadedTest("Sentry", cfg, msgPerf, ioPerf, statData, 10);
}
#endif

#ifdef ELOG_ENABLE_DATADOG_CONNECTOR
void testDatadog() {
    char* api_key = getenv("ELOG_DATADOG_API_KEY");
    if (api_key == nullptr) {
        fprintf(stderr, "Missing datadog API Key\n");
        return;
    }
    std::string cfg =
        "mon://datadog?address=https://http-intake.logs.datadoghq.eu&"
        "api_key=";
    cfg += std::string(api_key) + "&";
    cfg +=
        "source=elog&"
        "service=elog_bench&"
        "flush_policy=count&"
        "flush_count=5&"
        "tags={log_source=${src}, module=${mod}, file=${file}, line=${line}}&"
        "stack_trace=yes&"
        "compress=yes";
    double msgPerf = 0.0f;
    double ioPerf = 0.0f;
    StatData statData;
    runSingleThreadedTest("Datadog", cfg.c_str(), msgPerf, ioPerf, statData, 10);
}
#endif

#ifdef ELOG_ENABLE_OTEL_CONNECTOR
void testOtel() {
    std::string cfg =
        "mon://"
        "otel?method=http&endpoint=192.168.1.163:4318&debug=true&batching=yes&batch_export_size=25&"
        "log_format=msg:${rid}, ${time}, ${src}, ${mod}, ${tid}, ${pid}, ${file}, ${line}, "
        "${level}, ${msg}&"
        "flush_policy=count&flush_count=10";
    double msgPerf = 0.0f;
    double ioPerf = 0.0f;
    StatData statData;
    runSingleThreadedTest("Open-Telemetry", cfg.c_str(), msgPerf, ioPerf, statData, 10);

    // NOTE: grpc method works, but it cannot be run after http (process stuck with some lock) so
    // for unit tests we must do two separate runs
    /*cfg =
        "mon://otel?method=grpc&endpoint=192.168.1.163:4317&debug=true&"
        "log_format=msg:${rid}, ${time}, ${src}, ${mod}, ${tid}, ${pid}, ${file}, ${line}, "
        "${level}, ${msg}";
    runSingleThreadedTest("Open-Telemetry", cfg.c_str(), msgPerf, ioPerf, statData, 10);*/

    // TODO: regression test will launch a local otel collector and have it write records to file
    // then we can parse the log file and verify all records and attributes are there
}
#endif

int testColors() {
    const char* cfg =
        "sys://stderr?log_format=${time:font=faint} ${level:6:fg-color=green:bg-color=blue} "
        "[${tid:font=italic}] ${src:font=underline:fg-color=bright-red} "
        "${msg:font=cross-out,blink-rapid:fg-color=#993983}";
    elog::ELogTarget* logTarget = initElog(cfg);
    if (logTarget == nullptr) {
        return 1;
    }
    elog::ELogLogger* logger = elog::getPrivateLogger("elog_bench_logger");
    ELOG_INFO_EX(logger, "This is a test message");
    termELog();

    cfg =
        "sys://stderr?log_format=${time:font=faint} "
        "${if: (log_level == INFO): ${fmt:begin-fg-color=green}: ${fmt:begin-fg-color=red}}"
        "${level:6}${fmt:default} "
        "[${tid:font=italic}] ${src:font=underline:fg-color=bright-red} "
        "${msg:font=cross-out,blink-rapid:fg-color=#993983}";
    logTarget = initElog(cfg);
    if (logTarget == nullptr) {
        return 2;
    }
    logger = elog::getPrivateLogger("elog_bench_logger");
    ELOG_INFO_EX(logger, "This is a test message");
    ELOG_WARN_EX(logger, "This is a test message");
    termELog();

    cfg =
        "sys://stderr?log_format=${time:font=faint} "
        "${switch: ${level}:"
        "   ${case: ${const-level: INFO}: ${fmt:begin-fg-color=green}} :"
        "   ${case: ${const-level: WARN}: ${fmt:begin-fg-color=red}} :"
        "   ${case: ${const-level: ERROR}: ${fmt:begin-fg-color=magenta}} :"
        "   ${default: ${fmt:begin-fg-color=yellow}}}"
        "${level:6}${fmt:default} "
        "[${tid:font=italic}] ${src:font=underline:fg-color=bright-red} "
        "${msg:font=cross-out,blink-rapid:fg-color=#993983}";
    logTarget = initElog(cfg);
    if (logTarget == nullptr) {
        return 3;
    }
    logger = elog::getPrivateLogger("elog_bench_logger");
    ELOG_INFO_EX(logger, "This is a test message");
    ELOG_WARN_EX(logger, "This is a test message");
    ELOG_ERROR_EX(logger, "This is a test message");
    ELOG_NOTICE_EX(logger, "This is a test message");
    termELog();

    cfg =
        "sys://stderr?log_format=${time:font=faint} "
        "${expr-switch: "
        "   ${case: (log_level == INFO): ${fmt:begin-fg-color=green}} :"
        "   ${case: (log_level == WARN): ${fmt:begin-fg-color=red}} :"
        "   ${case: (log_level == ERROR): ${fmt:begin-fg-color=magenta}} :"
        "   ${default: ${fmt:begin-fg-color=yellow}}}"
        "${level:6}${fmt:default} "
        "[${tid:font=italic}] ${src:font=underline:fg-color=bright-red} "
        "${msg:font=cross-out,blink-rapid:fg-color=#993983}";
    logTarget = initElog(cfg);
    logger = elog::getPrivateLogger("elog_bench_logger");
    ELOG_INFO_EX(logger, "This is a test message");
    ELOG_WARN_EX(logger, "This is a test message");
    ELOG_ERROR_EX(logger, "This is a test message");
    ELOG_NOTICE_EX(logger, "This is a test message");
    termELog();
    return 0;
}

int testException() {
    sTestSingleAll = false;
    sTestSingleThreadQuantum = true;
    testPerfAllSingleThread();
    return 0;
}

// Get an index value to the pEventTypeNames array based on
// the event type value.
#ifdef ELOG_WINDOWS
const char* pEventTypeNames[] = {"Error", "Warning", "Informational", "Audit Success",
                                 "Audit Failure"};
DWORD GetEventTypeName(DWORD EventType) {
    DWORD index = 0;

    switch (EventType) {
        case EVENTLOG_ERROR_TYPE:
            index = 0;
            break;
        case EVENTLOG_WARNING_TYPE:
            index = 1;
            break;
        case EVENTLOG_INFORMATION_TYPE:
            index = 2;
            break;
        case EVENTLOG_AUDIT_SUCCESS:
            index = 3;
            break;
        case EVENTLOG_AUDIT_FAILURE:
            index = 4;
            break;
    }

    return index;
}
#define MAX_TIMESTAMP_LEN 64
void GetTimestamp(const DWORD Time, char displayString[]) {
    ULONGLONG ullTimeStamp = 0;
    ULONGLONG SecsTo1970 = 116444736000000000;
    SYSTEMTIME st;
    FILETIME ft, ftLocal;

    ullTimeStamp = Int32x32To64(Time, 10000000) + SecsTo1970;
    ft.dwHighDateTime = (DWORD)((ullTimeStamp >> 32) & 0xFFFFFFFF);
    ft.dwLowDateTime = (DWORD)(ullTimeStamp & 0xFFFFFFFF);

    FileTimeToLocalFileTime(&ft, &ftLocal);
    FileTimeToSystemTime(&ftLocal, &st);
    snprintf(displayString, MAX_TIMESTAMP_LEN, "%u-%.2u-%.2u %.2u:%.2u:%.2u.%.3u", st.wYear,
             st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
}
#endif

int testEventLog() {
#ifdef ELOG_WINDOWS
    const char* cfg = "sys://eventlog?event_source_name=elog_bench&event_id=1234&name=elog_bench";
    double msgPerf = 0.0f;
    double ioPerf = 0.0f;
    StatData statData;
    time_t testStartTime = time(NULL);
    runSingleThreadedTest("Win32 Event Log", cfg, msgPerf, ioPerf, statData, 10);

    // now we need to find the events in the event log
    HANDLE hLog = OpenEventLogA(NULL, "elog_bench");
    if (hLog == NULL) {
        ELOG_WIN32_ERROR(OpenEventLogA, "Could not open event log by name 'elog_bench");
        return 1;
    }

    EVENTLOGRECORD buffer[4096];
    DWORD bytesRead, minBytesNeeded;
    if (!ReadEventLogA(hLog, EVENTLOG_SEQUENTIAL_READ | EVENTLOG_BACKWARDS_READ, 0, &buffer,
                       sizeof(buffer), &bytesRead, &minBytesNeeded)) {
        ELOG_WIN32_ERROR(ReadEventLogA, "Could not read event log by name 'elog_bench");
        return 2;
    }

    // read recent events backwards and verify test result
    // we expect to see exactly 13 records (due to pre-init 2 log messages, and one test error
    // message at runSingleThreadedTest), which belong to elog_bench provider and have a higher
    // timestamp, and we should stop when timestamp goes beyond test start time
    uint32_t matchingRecords = 0;
    PBYTE pRecord = (PBYTE)buffer;
    PBYTE pEndOfRecords = (PBYTE)(buffer + bytesRead);
    while (pRecord < pEndOfRecords) {
        PEVENTLOGRECORD eventRecord = (PEVENTLOGRECORD)pRecord;
        if (eventRecord->TimeGenerated < testStartTime) {
            break;
        }
        char* providerName = (char*)(pRecord + sizeof(EVENTLOGRECORD));
        uint32_t statusCode = eventRecord->EventID & 0xFFFF;
        if ((strcmp(providerName, "elog_bench") == 0) && statusCode == 1234) {
            printf("provider name: %s\n", providerName);
            printf("status code: %d\n", statusCode);
            char timeStamp[MAX_TIMESTAMP_LEN];
            GetTimestamp(eventRecord->TimeGenerated, timeStamp);
            printf("Time stamp: %s\n", timeStamp);
            printf("record number: %lu\n", eventRecord->RecordNumber);
            printf("event type: %s\n", pEventTypeNames[GetEventTypeName(eventRecord->EventType)]);
            char* pMessage = (char*)(pRecord + eventRecord->StringOffset);
            if (pMessage != nullptr) {
                printf("event first string arg: %s\n", pMessage);
            }
            printf("\n");
            fflush(stdout);

            ++matchingRecords;
        }
        pRecord += eventRecord->Length;
    }

    CloseEventLog(hLog);
    if (matchingRecords != 13) {
        fprintf(stderr, "Event Log test failed, expecting 13 records, but instead found %u\n",
                matchingRecords);
        return 3;
    }
    return 0;
#else
    return -1;
#endif
}

#ifdef ELOG_MSVC
inline std::string win32FormatNumber(double number, unsigned precision = 3) {
    char fmtStr[32];
    snprintf(fmtStr, 32, "%%.%uf", precision);
    char numStr[32] = {};
    snprintf(numStr, 32, fmtStr, number);
    char buf[32] = {};
    NUMBERFMTA nf = {2, 0, 3, (LPSTR) ".", (LPSTR) ",", 1};
    GetNumberFormatA(LOCALE_NAME_USER_DEFAULT, 0, numStr, &nf, buf, 32);
    return buf;
}
#endif

void runSingleThreadedTest(const char* title, const char* cfg, double& msgThroughput,
                           double& ioThroughput, StatData& msgPercentile,
                           uint32_t msgCount /* = ST_MSG_COUNT */, bool enableTrace /* = false */) {
    if (sMsgCnt > 0) {
        msgCount = sMsgCnt;
    }
    elog::ELogTarget* logTarget = initElog(cfg);
    if (logTarget == nullptr) {
        fprintf(stderr, "Failed to init %s test, aborting\n", title);
        return;
    }

    if (enableTrace) {
        elog::setReportLevel(elog::ELEVEL_TRACE);
    }

    fprintf(stderr, "\nRunning %s single-thread test\n", title);
    elog::ELogSource* logSource = elog::defineLogSource("elog.bench", true);
    elog::ELogLogger* logger = logSource->createPrivateLogger();
#ifdef MEASURE_PERCENTILE
    std::vector<double> samples(msgCount, 0.0f);
#endif

    if (sTestException) {
        int msg = 0;
        fprintf(stderr, "Exception test\n");
        uint64_t inverse = 1 / msg;
        uint64_t* ptr = nullptr;
        *ptr = inverse;
    }

    uint64_t bytesStart = logTarget->getBytesWritten();
    pinThread(0);
    auto start = std::chrono::high_resolution_clock::now();
    for (uint64_t i = 0; i < msgCount; ++i) {
#ifdef MEASURE_PERCENTILE
        auto logStart = std::chrono::high_resolution_clock::now();
#endif
        ELOG_INFO_EX(logger, "Single thread Test log %u", i);
#ifdef MEASURE_PERCENTILE
        auto logEnd = std::chrono::high_resolution_clock::now();
        samples[i] =
            std::chrono::duration_cast<std::chrono::microseconds>(logEnd - logStart).count();
#endif
    }
    auto end0 = std::chrono::high_resolution_clock::now();
    fprintf(stderr, "Finished logging, waiting for logger to catch up\n");
    logTarget->flush();
    while (!isCaughtUp(logTarget, msgCount)) {
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

    msgThroughput = msgCount / (double)testTime0.count() * 1000000.0f;
    ioThroughput = (bytesEnd - bytesStart) / (double)testTime.count() * 1000000.0f / 1024;
#ifdef ELOG_MSVC
    fprintf(stderr, "Throughput: %s MSg/Sec\n", win32FormatNumber(msgThroughput).c_str());
    fprintf(stderr, "Throughput: %s KB/Sec\n\n", win32FormatNumber(ioThroughput).c_str());
#else
    fprintf(stderr, "Throughput: %'.3f MSg/Sec\n", msgThroughput);
    fprintf(stderr, "Throughput: %'.3f KB/Sec\n\n", ioThroughput);
#endif

#ifdef MEASURE_PERCENTILE
    getSamplePercentiles(samples, msgPercentile);
#endif

    termELog();
}

#ifdef ELOG_ENABLE_FMT_LIB
void runSingleThreadedTestBinary(const char* title, const char* cfg, double& msgThroughput,
                                 double& ioThroughput, StatData& msgPercentile,
                                 uint32_t msgCount /* = ST_MSG_COUNT */,
                                 bool enableTrace /* = false */) {
    if (sMsgCnt > 0) {
        msgCount = sMsgCnt;
    }
    elog::ELogTarget* logTarget = initElog(cfg);
    if (logTarget == nullptr) {
        fprintf(stderr, "Failed to init %s test, aborting\n", title);
        return;
    }

    if (enableTrace) {
        elog::setReportLevel(elog::ELEVEL_TRACE);
    }

    fprintf(stderr, "\nRunning %s single-thread test\n", title);
    elog::ELogSource* logSource = elog::defineLogSource("elog.bench", true);
    elog::ELogLogger* logger = logSource->createPrivateLogger();
#ifdef MEASURE_PERCENTILE
    std::vector<double> samples(msgCount, 0.0f);
#endif
    ELOG_ERROR_EX(logger, "This is a test error message");

    if (sTestException) {
        int msg = 0;
        fprintf(stderr, "Exception test\n");
        uint64_t inverse = 1 / msg;
        uint64_t* ptr = nullptr;
        *ptr = inverse;
    }

    uint64_t bytesStart = logTarget->getBytesWritten();
    auto start = std::chrono::high_resolution_clock::now();
    for (uint64_t i = 0; i < msgCount; ++i) {
#ifdef MEASURE_PERCENTILE
        auto logStart = std::chrono::high_resolution_clock::now();
#endif
        ELOG_BIN_INFO_EX(logger, "Single thread Test log {}", i);
#ifdef MEASURE_PERCENTILE
        auto logEnd = std::chrono::high_resolution_clock::now();
        samples[i] =
            std::chrono::duration_cast<std::chrono::microseconds>(logEnd - logStart).count();
#endif
    }
    auto end0 = std::chrono::high_resolution_clock::now();
    fprintf(stderr, "Finished logging, waiting for logger to catch up\n");
    while (!isCaughtUp(logTarget, msgCount)) {
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

    msgThroughput = msgCount / (double)testTime0.count() * 1000000.0f;
    fprintf(stderr, "Throughput: %0.3f MSg/Sec\n", msgThroughput);

    ioThroughput = (bytesEnd - bytesStart) / (double)testTime.count() * 1000000.0f / 1024;
    fprintf(stderr, "Throughput: %0.3f KB/Sec\n\n", ioThroughput);
#ifdef MEASURE_PERCENTILE
    getSamplePercentiles(samples, msgPercentile);
#endif

    termELog();
}

void runSingleThreadedTestBinaryCached(const char* title, const char* cfg, double& msgThroughput,
                                       double& ioThroughput, StatData& msgPercentile,
                                       uint32_t msgCount /* = ST_MSG_COUNT */,
                                       bool enableTrace /* = false */) {
    if (sMsgCnt > 0) {
        msgCount = sMsgCnt;
    }
    elog::ELogTarget* logTarget = initElog(cfg);
    if (logTarget == nullptr) {
        fprintf(stderr, "Failed to init %s test, aborting\n", title);
        return;
    }

    if (enableTrace) {
        elog::setReportLevel(elog::ELEVEL_TRACE);
    }

    fprintf(stderr, "\nRunning %s single-thread test\n", title);
    elog::ELogSource* logSource = elog::defineLogSource("elog.bench", true);
    elog::ELogLogger* logger = logSource->createPrivateLogger();
#ifdef MEASURE_PERCENTILE
    std::vector<double> samples(msgCount, 0.0f);
#endif
    ELOG_ERROR_EX(logger, "This is a test error message");

    if (sTestException) {
        int msg = 0;
        fprintf(stderr, "Exception test\n");
        uint64_t inverse = 1 / msg;
        uint64_t* ptr = nullptr;
        *ptr = inverse;
    }

    uint64_t bytesStart = logTarget->getBytesWritten();
    auto start = std::chrono::high_resolution_clock::now();
    for (uint64_t i = 0; i < msgCount; ++i) {
#ifdef MEASURE_PERCENTILE
        auto logStart = std::chrono::high_resolution_clock::now();
#endif
        ELOG_CACHE_INFO_EX(logger, "Single thread Test log {}", i);
#ifdef MEASURE_PERCENTILE
        auto logEnd = std::chrono::high_resolution_clock::now();
        samples[i] =
            std::chrono::duration_cast<std::chrono::microseconds>(logEnd - logStart).count();
#endif
    }
    auto end0 = std::chrono::high_resolution_clock::now();
    fprintf(stderr, "Finished logging, waiting for logger to catch up\n");
    while (!isCaughtUp(logTarget, msgCount)) {
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

    msgThroughput = msgCount / (double)testTime0.count() * 1000000.0f;
    fprintf(stderr, "Throughput: %0.3f MSg/Sec\n", msgThroughput);

    ioThroughput = (bytesEnd - bytesStart) / (double)testTime.count() * 1000000.0f / 1024;
    fprintf(stderr, "Throughput: %0.3f KB/Sec\n\n", ioThroughput);
#ifdef MEASURE_PERCENTILE
    getSamplePercentiles(samples, msgPercentile);
#endif

    termELog();
}

void runSingleThreadedTestBinaryPreCached(const char* title, const char* cfg, double& msgThroughput,
                                          double& ioThroughput, StatData& msgPercentile,
                                          uint32_t msgCount /* = ST_MSG_COUNT */,
                                          bool enableTrace /* = false */) {
    if (sMsgCnt > 0) {
        msgCount = sMsgCnt;
    }
    elog::ELogTarget* logTarget = initElog(cfg);
    if (logTarget == nullptr) {
        fprintf(stderr, "Failed to init %s test, aborting\n", title);
        return;
    }

    if (enableTrace) {
        elog::setReportLevel(elog::ELEVEL_TRACE);
    }

    fprintf(stderr, "\nRunning %s single-thread test\n", title);
    elog::ELogSource* logSource = elog::defineLogSource("elog.bench", true);
    elog::ELogLogger* logger = logSource->createPrivateLogger();
#ifdef MEASURE_PERCENTILE
    std::vector<double> samples(msgCount, 0.0f);
#endif
    ELOG_ERROR_EX(logger, "This is a test error message");

    if (sTestException) {
        int msg = 0;
        fprintf(stderr, "Exception test\n");
        uint64_t inverse = 1 / msg;
        uint64_t* ptr = nullptr;
        *ptr = inverse;
    }

    elog::ELogCacheEntryId msgId = elog::getOrCacheFormatMsg("Single thread Test log {}");
    uint64_t bytesStart = logTarget->getBytesWritten();
    auto start = std::chrono::high_resolution_clock::now();
    for (uint64_t i = 0; i < msgCount; ++i) {
#ifdef MEASURE_PERCENTILE
        auto logStart = std::chrono::high_resolution_clock::now();
#endif
        ELOG_ID_INFO_EX(logger, msgId, i);
#ifdef MEASURE_PERCENTILE
        auto logEnd = std::chrono::high_resolution_clock::now();
        samples[i] =
            std::chrono::duration_cast<std::chrono::microseconds>(logEnd - logStart).count();
#endif
    }
    auto end0 = std::chrono::high_resolution_clock::now();
    fprintf(stderr, "Finished logging, waiting for logger to catch up\n");
    while (!isCaughtUp(logTarget, msgCount)) {
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

    msgThroughput = msgCount / (double)testTime0.count() * 1000000.0f;
    fprintf(stderr, "Throughput: %0.3f MSg/Sec\n", msgThroughput);

    ioThroughput = (bytesEnd - bytesStart) / (double)testTime.count() * 1000000.0f / 1024;
    fprintf(stderr, "Throughput: %0.3f KB/Sec\n\n", ioThroughput);
#ifdef MEASURE_PERCENTILE
    getSamplePercentiles(samples, msgPercentile);
#endif

    termELog();
}
#endif

void runMultiThreadTest(const char* title, const char* fileName, const char* cfg,
                        bool privateLogger /* = true */,
                        uint32_t minThreads /* = MIN_THREAD_COUNT */,
                        uint32_t maxThreads /* = MAX_THREAD_COUNT */,
                        bool enableTrace /* = false */) {
    if (sMinThreadCnt > 0) {
        minThreads = sMinThreadCnt;
    }
    if (sMaxThreadCnt > 0) {
        maxThreads = sMaxThreadCnt;
    }
    uint32_t msgCount = MT_MSG_COUNT;
    if (sMsgCnt > 0) {
        msgCount = sMsgCnt;
    }
    elog::ELogTarget* logTarget = initElog(cfg);
    if (logTarget == nullptr) {
        fprintf(stderr, "Failed to init %s test, aborting\n", title);
        return;
    }

    if (enableTrace) {
        elog::setReportLevel(elog::ELEVEL_TRACE);
    }

    fprintf(stderr, "\nRunning %s thread test [%u-%u]\n", title, minThreads, maxThreads);
    std::vector<double> msgThroughput;
    std::vector<double> byteThroughput;
    std::vector<double> accumThroughput;
    elog::ELogLogger* sharedLogger =
        privateLogger ? nullptr : elog::getSharedLogger("elog_bench_logger");
    for (uint32_t i = MIN_THREAD_COUNT; i < minThreads; ++i) {
        msgThroughput.push_back(0);
        byteThroughput.push_back(0);
        accumThroughput.push_back(0);
    }
    for (uint32_t i = maxThreads + 1; i < MAX_THREAD_COUNT; ++i) {
        msgThroughput.push_back(0);
        byteThroughput.push_back(0);
        accumThroughput.push_back(0);
    }
    for (uint32_t threadCount = minThreads; threadCount <= maxThreads; ++threadCount) {
        // fprintf(stderr, "Running %u threads test\n", threadCount);
        ELOG_INFO("Running %u Thread Test", threadCount);
        std::vector<std::thread> threads;
        std::vector<double> resVec(threadCount, 0.0);
        auto start = std::chrono::high_resolution_clock::now();
        std::vector<elog::ELogLogger*> loggers(threadCount);
        // create private loggers before running threads, otherwise race condition may happen
        // (log source is not thread-safe)
        for (uint32_t i = 0; i < threadCount; ++i) {
            loggers[i] = sharedLogger != nullptr ? sharedLogger
                                                 : elog::getPrivateLogger("elog_bench_logger");
        }
        uint64_t bytesStart = logTarget->getBytesWritten();
        uint64_t initMsgCount = logTarget->getProcessedMsgCount();
        // fprintf(stderr, "Init msg count = %" PRIu64 "\n", initMsgCount);
        for (uint32_t i = 0; i < threadCount; ++i) {
            elog::ELogLogger* logger = loggers[i];
            threads.emplace_back(std::thread([i, &resVec, logger, msgCount]() {
                std::string tname = std::string("worker-") + std::to_string(i);
                elog::setCurrentThreadName(tname.c_str());
                pinThread(i);
                auto start = std::chrono::high_resolution_clock::now();
                for (uint64_t j = 0; j < msgCount; ++j) {
                    ELOG_INFO_EX(logger, "Thread %u Test log %u", i, j);
                }
                auto end = std::chrono::high_resolution_clock::now();
                std::chrono::microseconds testTime =
                    std::chrono::duration_cast<std::chrono::microseconds>(end - start);
                double throughput = msgCount / (double)testTime.count() * 1000000.0f;
                /*fprintf(stderr, "Test time: %u usec, msg count: %u\n",
                (unsigned)testTime.count(), (unsigned)msgCount); fprintf(stderr, "Throughput:
                %0.3f MSg/Sec\n", throughput);*/
                resVec[i] = throughput;
            }));
        }
        for (uint32_t i = 0; i < threadCount; ++i) {
            threads[i].join();
        }
        auto end0 = std::chrono::high_resolution_clock::now();
        fprintf(stderr, "Finished logging, waiting for logger to catch up\n");
        uint64_t targetMsgCount = initMsgCount + threadCount * msgCount;
        // fprintf(stderr, "Waiting for target msg count %" PRIu64 "\n", targetMsgCount);
        logTarget->flush();  // required for net/ipc tests
        while (!isCaughtUp(logTarget, targetMsgCount)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(0));
        }

        auto end = std::chrono::high_resolution_clock::now();
        ELOG_INFO("%u Thread Test ended", threadCount);
        uint64_t bytesEnd = logTarget->getBytesWritten();
        double throughput = 0;
        for (double val : resVec) {
            throughput += val;
        }
#ifdef ELOG_MSVC
        fprintf(stderr, "%u thread accumulated throughput: %s Msg/Sec\n", threadCount,
                win32FormatNumber(throughput, 2).c_str());
#else
        fprintf(stderr, "%u thread accumulated throughput: %'.2f Msg/Sec\n", threadCount,
                throughput);
#endif
        accumThroughput.push_back(throughput);

        std::chrono::microseconds testTime0 =
            std::chrono::duration_cast<std::chrono::microseconds>(end0 - start);
        std::chrono::microseconds testTime =
            std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        throughput = threadCount * msgCount / (double)testTime0.count() * 1000000.0f;
        /*fprintf(stderr, "%u thread Test time: %u usec, msg count: %u\n", threadCount,
                (unsigned)testTime.count(), (unsigned)MSG_COUNT);*/
#ifdef ELOG_MSVC
        fprintf(stderr, "%u thread Throughput: %s MSg/Sec\n", threadCount,
                win32FormatNumber(throughput).c_str());
#else
        fprintf(stderr, "%u thread Throughput: %'.3f MSg/Sec\n", threadCount, throughput);
#endif
        msgThroughput.push_back(throughput);
        throughput = (bytesEnd - bytesStart) / (double)testTime.count() * 1000000.0f / 1024;
#ifdef ELOG_MSVC
        fprintf(stderr, "%u thread Throughput: %s KB/Sec\n\n", threadCount,
                win32FormatNumber(throughput).c_str());
#else
        fprintf(stderr, "%u thread Throughput: %'.3f KB/Sec\n\n", threadCount, throughput);
#endif
        byteThroughput.push_back(throughput);
    }

    // std::this_thread::sleep_for(std::chrono::milliseconds(5000));
    termELog();

    // printMermaidChart(title, msgThroughput, byteThroughput);
    // printMarkdownTable(title, msgThroughput, byteThroughput);
    writeCsvFile(fileName, msgThroughput, byteThroughput, accumThroughput, privateLogger);
}

#ifdef ELOG_ENABLE_FMT_LIB
void runMultiThreadTestBinary(const char* title, const char* fileName, const char* cfg,
                              bool privateLogger /* = true */,
                              uint32_t minThreads /* = MIN_THREAD_COUNT */,
                              uint32_t maxThreads /* = MAX_THREAD_COUNT */,
                              bool enableTrace /* = false */) {
    if (sMinThreadCnt > 0) {
        minThreads = sMinThreadCnt;
    }
    if (sMaxThreadCnt > 0) {
        maxThreads = sMaxThreadCnt;
    }
    uint32_t msgCount = MT_MSG_COUNT;
    if (sMsgCnt > 0) {
        msgCount = sMsgCnt;
    }
    elog::ELogTarget* logTarget = initElog(cfg);
    if (logTarget == nullptr) {
        fprintf(stderr, "Failed to init %s test, aborting\n", title);
        return;
    }

    if (enableTrace) {
        elog::setReportLevel(elog::ELEVEL_TRACE);
    }

    fprintf(stderr, "\nRunning %s thread test [%u-%u]\n", title, minThreads, maxThreads);
    std::vector<double> msgThroughput;
    std::vector<double> byteThroughput;
    std::vector<double> accumThroughput;
    elog::ELogLogger* sharedLogger =
        privateLogger ? nullptr : elog::getSharedLogger("elog_bench_logger");
    for (uint32_t i = MIN_THREAD_COUNT; i < minThreads; ++i) {
        msgThroughput.push_back(0);
        byteThroughput.push_back(0);
        accumThroughput.push_back(0);
    }
    for (uint32_t i = maxThreads + 1; i < MAX_THREAD_COUNT; ++i) {
        msgThroughput.push_back(0);
        byteThroughput.push_back(0);
        accumThroughput.push_back(0);
    }
    for (uint32_t threadCount = minThreads; threadCount <= maxThreads; ++threadCount) {
        // fprintf(stderr, "Running %u threads test\n", threadCount);
        ELOG_INFO("Running %u Thread Test", threadCount);
        std::vector<std::thread> threads;
        std::vector<double> resVec(threadCount, 0.0);
        auto start = std::chrono::high_resolution_clock::now();
        std::vector<elog::ELogLogger*> loggers(threadCount);
        // create private loggers before running threads, otherwise race condition may happen
        // (log source is not thread-safe)
        for (uint32_t i = 0; i < threadCount; ++i) {
            loggers[i] = sharedLogger != nullptr ? sharedLogger
                                                 : elog::getPrivateLogger("elog_bench_logger");
        }
        uint64_t bytesStart = logTarget->getBytesWritten();
        for (uint32_t i = 0; i < threadCount; ++i) {
            elog::ELogLogger* logger = loggers[i];
            threads.emplace_back(std::thread([i, &resVec, logger, msgCount]() {
                std::string tname = std::string("worker-") + std::to_string(i);
                elog::setCurrentThreadName(tname.c_str());
                pinThread(i);
                auto start = std::chrono::high_resolution_clock::now();
                for (uint64_t j = 0; j < msgCount; ++j) {
                    ELOG_BIN_INFO_EX(logger, "Thread {} Test log {}", i, j);
                }
                auto end = std::chrono::high_resolution_clock::now();
                std::chrono::microseconds testTime =
                    std::chrono::duration_cast<std::chrono::microseconds>(end - start);
                double throughput = msgCount / (double)testTime.count() * 1000000.0f;
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
        fprintf(stderr, "Finished logging, waiting for logger to catch up\n");

        while (!isCaughtUp(logTarget, threadCount * msgCount)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(0));
        }

        auto end = std::chrono::high_resolution_clock::now();
        ELOG_INFO("%u Thread Test ended", threadCount);
        uint64_t bytesEnd = logTarget->getBytesWritten();
        double throughput = 0;
        for (double val : resVec) {
            throughput += val;
        }
#ifdef ELOG_MSVC
        fprintf(stderr, "%u thread accumulated throughput: %s\n", threadCount,
                win32FormatNumber(throughput, 2).c_str());
#else
        fprintf(stderr, "%u thread accumulated throughput: %'.2f\n", threadCount, throughput);
#endif
        accumThroughput.push_back(throughput);

        std::chrono::microseconds testTime0 =
            std::chrono::duration_cast<std::chrono::microseconds>(end0 - start);
        std::chrono::microseconds testTime =
            std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        throughput = threadCount * msgCount / (double)testTime0.count() * 1000000.0f;
        /*fprintf(stderr, "%u thread Test time: %u usec, msg count: %u\n", threadCount,
                (unsigned)testTime.count(), (unsigned)MSG_COUNT);*/
#ifdef ELOG_MSVC
        fprintf(stderr, "%u thread Throughput: %s MSg/Sec\n", threadCount,
                win32FormatNumber(throughput).c_str());
#else
        fprintf(stderr, "%u thread Throughput: %'.3f MSg/Sec\n", threadCount, throughput);
#endif
        msgThroughput.push_back(throughput);
        throughput = (bytesEnd - bytesStart) / (double)testTime.count() * 1000000.0f / 1024;
#ifdef ELOG_MSVC
        fprintf(stderr, "%u thread Throughput: %s KB/Sec\n\n", threadCount,
                win32FormatNumber(throughput).c_str());
#else
        fprintf(stderr, "%u thread Throughput: %'.3f KB/Sec\n\n", threadCount, throughput);
#endif
        byteThroughput.push_back(throughput);
    }

    // std::this_thread::sleep_for(std::chrono::milliseconds(5000));
    termELog();

    // printMermaidChart(title, msgThroughput, byteThroughput);
    // printMarkdownTable(title, msgThroughput, byteThroughput);
    writeCsvFile(fileName, msgThroughput, byteThroughput, accumThroughput, privateLogger);
}

void runMultiThreadTestBinaryCached(const char* title, const char* fileName, const char* cfg,
                                    bool privateLogger /* = true */,
                                    uint32_t minThreads /* = MIN_THREAD_COUNT */,
                                    uint32_t maxThreads /* = MAX_THREAD_COUNT */,
                                    bool enableTrace /* = false */) {
    if (sMinThreadCnt > 0) {
        minThreads = sMinThreadCnt;
    }
    if (sMaxThreadCnt > 0) {
        maxThreads = sMaxThreadCnt;
    }
    uint32_t msgCount = MT_MSG_COUNT;
    if (sMsgCnt > 0) {
        msgCount = sMsgCnt;
    }
    elog::ELogTarget* logTarget = initElog(cfg);
    if (logTarget == nullptr) {
        fprintf(stderr, "Failed to init %s test, aborting\n", title);
        return;
    }

    if (enableTrace) {
        elog::setReportLevel(elog::ELEVEL_TRACE);
    }

    fprintf(stderr, "\nRunning %s thread test [%u-%u]\n", title, minThreads, maxThreads);
    std::vector<double> msgThroughput;
    std::vector<double> byteThroughput;
    std::vector<double> accumThroughput;
    elog::ELogLogger* sharedLogger =
        privateLogger ? nullptr : elog::getSharedLogger("elog_bench_logger");
    for (uint32_t i = MIN_THREAD_COUNT; i < minThreads; ++i) {
        msgThroughput.push_back(0);
        byteThroughput.push_back(0);
        accumThroughput.push_back(0);
    }
    for (uint32_t i = maxThreads + 1; i < MAX_THREAD_COUNT; ++i) {
        msgThroughput.push_back(0);
        byteThroughput.push_back(0);
        accumThroughput.push_back(0);
    }
    for (uint32_t threadCount = minThreads; threadCount <= maxThreads; ++threadCount) {
        // fprintf(stderr, "Running %u threads test\n", threadCount);
        ELOG_INFO("Running %u Thread Test", threadCount);
        std::vector<std::thread> threads;
        std::vector<double> resVec(threadCount, 0.0);
        auto start = std::chrono::high_resolution_clock::now();
        std::vector<elog::ELogLogger*> loggers(threadCount);
        // create private loggers before running threads, otherwise race condition may happen
        // (log source is not thread-safe)
        for (uint32_t i = 0; i < threadCount; ++i) {
            loggers[i] = sharedLogger != nullptr ? sharedLogger
                                                 : elog::getPrivateLogger("elog_bench_logger");
        }
        uint64_t bytesStart = logTarget->getBytesWritten();
        for (uint32_t i = 0; i < threadCount; ++i) {
            elog::ELogLogger* logger = loggers[i];
            threads.emplace_back(std::thread([i, &resVec, logger, msgCount]() {
                std::string tname = std::string("worker-") + std::to_string(i);
                elog::setCurrentThreadName(tname.c_str());
                pinThread(i);
                auto start = std::chrono::high_resolution_clock::now();
                for (uint64_t j = 0; j < msgCount; ++j) {
                    ELOG_CACHE_INFO_EX(logger, "Thread {} Test log {}", i, j);
                }
                auto end = std::chrono::high_resolution_clock::now();
                std::chrono::microseconds testTime =
                    std::chrono::duration_cast<std::chrono::microseconds>(end - start);
                double throughput = msgCount / (double)testTime.count() * 1000000.0f;
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
        fprintf(stderr, "Finished logging, waiting for logger to catch up\n");
        while (!isCaughtUp(logTarget, threadCount * msgCount)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(0));
        }

        auto end = std::chrono::high_resolution_clock::now();
        ELOG_INFO("%u Thread Test ended", threadCount);
        uint64_t bytesEnd = logTarget->getBytesWritten();
        double throughput = 0;
        for (double val : resVec) {
            throughput += val;
        }
#ifdef ELOG_MSVC
        fprintf(stderr, "%u thread accumulated throughput: %s\n", threadCount,
                win32FormatNumber(throughput, 2).c_str());
#else
        fprintf(stderr, "%u thread accumulated throughput: %'.2f\n", threadCount, throughput);
#endif
        accumThroughput.push_back(throughput);

        std::chrono::microseconds testTime0 =
            std::chrono::duration_cast<std::chrono::microseconds>(end0 - start);
        std::chrono::microseconds testTime =
            std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        throughput = threadCount * msgCount / (double)testTime0.count() * 1000000.0f;
        /*fprintf(stderr, "%u thread Test time: %u usec, msg count: %u\n", threadCount,
                (unsigned)testTime.count(), (unsigned)MSG_COUNT);*/
#ifdef ELOG_MSVC
        fprintf(stderr, "%u thread Throughput: %s MSg/Sec\n", threadCount,
                win32FormatNumber(throughput).c_str());
#else
        fprintf(stderr, "%u thread Throughput: %'.3f MSg/Sec\n", threadCount, throughput);
#endif
        msgThroughput.push_back(throughput);
        throughput = (bytesEnd - bytesStart) / (double)testTime.count() * 1000000.0f / 1024;
#ifdef ELOG_MSVC
        fprintf(stderr, "%u thread Throughput: %s KB/Sec\n\n", threadCount,
                win32FormatNumber(throughput).c_str());
#else
        fprintf(stderr, "%u thread Throughput: %'.3f KB/Sec\n\n", threadCount, throughput);
#endif
        byteThroughput.push_back(throughput);
    }

    // std::this_thread::sleep_for(std::chrono::milliseconds(5000));
    termELog();

    // printMermaidChart(title, msgThroughput, byteThroughput);
    // printMarkdownTable(title, msgThroughput, byteThroughput);
    writeCsvFile(fileName, msgThroughput, byteThroughput, accumThroughput, privateLogger);
}

void runMultiThreadTestBinaryPreCached(const char* title, const char* fileName, const char* cfg,
                                       bool privateLogger /* = true */,
                                       uint32_t minThreads /* = MIN_THREAD_COUNT */,
                                       uint32_t maxThreads /* = MAX_THREAD_COUNT */,
                                       bool enableTrace /* = false */) {
    if (sMinThreadCnt > 0) {
        minThreads = sMinThreadCnt;
    }
    if (sMaxThreadCnt > 0) {
        maxThreads = sMaxThreadCnt;
    }
    uint32_t msgCount = MT_MSG_COUNT;
    if (sMsgCnt > 0) {
        msgCount = sMsgCnt;
    }
    elog::ELogTarget* logTarget = initElog(cfg);
    if (logTarget == nullptr) {
        fprintf(stderr, "Failed to init %s test, aborting\n", title);
        return;
    }

    if (enableTrace) {
        elog::setReportLevel(elog::ELEVEL_TRACE);
    }

    fprintf(stderr, "\nRunning %s thread test [%u-%u]\n", title, minThreads, maxThreads);
    std::vector<double> msgThroughput;
    std::vector<double> byteThroughput;
    std::vector<double> accumThroughput;
    elog::ELogLogger* sharedLogger =
        privateLogger ? nullptr : elog::getSharedLogger("elog_bench_logger");
    for (uint32_t i = MIN_THREAD_COUNT; i < minThreads; ++i) {
        msgThroughput.push_back(0);
        byteThroughput.push_back(0);
        accumThroughput.push_back(0);
    }
    for (uint32_t i = maxThreads + 1; i < MAX_THREAD_COUNT; ++i) {
        msgThroughput.push_back(0);
        byteThroughput.push_back(0);
        accumThroughput.push_back(0);
    }
    elog::ELogCacheEntryId msgId = elog::getOrCacheFormatMsg("Thread {} Test log {}");
    for (uint32_t threadCount = minThreads; threadCount <= maxThreads; ++threadCount) {
        // fprintf(stderr, "Running %u threads test\n", threadCount);
        ELOG_INFO("Running %u Thread Test", threadCount);
        std::vector<std::thread> threads;
        std::vector<double> resVec(threadCount, 0.0);
        auto start = std::chrono::high_resolution_clock::now();
        std::vector<elog::ELogLogger*> loggers(threadCount);
        // create private loggers before running threads, otherwise race condition may happen
        // (log source is not thread-safe)
        for (uint32_t i = 0; i < threadCount; ++i) {
            loggers[i] = sharedLogger != nullptr ? sharedLogger
                                                 : elog::getPrivateLogger("elog_bench_logger");
        }
        uint64_t bytesStart = logTarget->getBytesWritten();
        for (uint32_t i = 0; i < threadCount; ++i) {
            elog::ELogLogger* logger = loggers[i];
            threads.emplace_back(std::thread([i, msgId, &resVec, logger, msgCount]() {
                std::string tname = std::string("worker-") + std::to_string(i);
                elog::setCurrentThreadName(tname.c_str());
                pinThread(i);
                auto start = std::chrono::high_resolution_clock::now();
                for (uint64_t j = 0; j < msgCount; ++j) {
                    ELOG_ID_INFO_EX(logger, msgId, i, j);
                }
                auto end = std::chrono::high_resolution_clock::now();
                std::chrono::microseconds testTime =
                    std::chrono::duration_cast<std::chrono::microseconds>(end - start);
                double throughput = msgCount / (double)testTime.count() * 1000000.0f;
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
        fprintf(stderr, "Finished logging, waiting for logger to catch up\n");

        while (!isCaughtUp(logTarget, threadCount * msgCount)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(0));
        }

        auto end = std::chrono::high_resolution_clock::now();
        ELOG_INFO("%u Thread Test ended", threadCount);
        uint64_t bytesEnd = logTarget->getBytesWritten();
        double throughput = 0;
        for (double val : resVec) {
            throughput += val;
        }
#ifdef ELOG_MSVC
        fprintf(stderr, "%u thread accumulated throughput: %s\n", threadCount,
                win32FormatNumber(throughput, 2).c_str());
#else
        fprintf(stderr, "%u thread accumulated throughput: %'.2f\n", threadCount, throughput);
#endif
        accumThroughput.push_back(throughput);

        std::chrono::microseconds testTime0 =
            std::chrono::duration_cast<std::chrono::microseconds>(end0 - start);
        std::chrono::microseconds testTime =
            std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        throughput = threadCount * msgCount / (double)testTime0.count() * 1000000.0f;
        /*fprintf(stderr, "%u thread Test time: %u usec, msg count: %u\n", threadCount,
                (unsigned)testTime.count(), (unsigned)MSG_COUNT);*/
#ifdef ELOG_MSVC
        fprintf(stderr, "%u thread Throughput: %s MSg/Sec\n", threadCount,
                win32FormatNumber(throughput).c_str());
#else
        fprintf(stderr, "%u thread Throughput: %'.3f MSg/Sec\n", threadCount, throughput);
#endif
        msgThroughput.push_back(throughput);
        throughput = (bytesEnd - bytesStart) / (double)testTime.count() * 1000000.0f / 1024;
#ifdef ELOG_MSVC
        fprintf(stderr, "%u thread Throughput: %s KB/Sec\n\n", threadCount,
                win32FormatNumber(throughput).c_str());
#else
        fprintf(stderr, "%u thread Throughput: %'.3f KB/Sec\n\n", threadCount, throughput);
#endif
        byteThroughput.push_back(throughput);
    }

    // std::this_thread::sleep_for(std::chrono::milliseconds(5000));
    termELog();

    // printMermaidChart(title, msgThroughput, byteThroughput);
    // printMarkdownTable(title, msgThroughput, byteThroughput);
    writeCsvFile(fileName, msgThroughput, byteThroughput, accumThroughput, privateLogger);
}
#endif

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
    if (sTestFileAll || sTestFileNever) {
        testPerfFileNeverFlushPolicy();
    }

    // flush immediate
    if (sTestFileAll || sTestFileImmediate) {
        testPerfImmediateFlushPolicy();
    }

    // group flush - not part of total performance test, can only test separately
    // because group flush is good only for thread thrashing scenario
    if (/*sTestFileAll ||*/ sTestFileGroup) {
        testPerfGroupFlushPolicy();
    }

    // flush count
    if (sTestFileAll || sTestFileCount) {
        testPerfCountFlushPolicy();
    }

    // flush size
    if (sTestFileAll || sTestFileSize) {
        testPerfSizeFlushPolicy();
    }

    // flush time
    if (sTestFileAll || sTestFileTime) {
        testPerfTimeFlushPolicy();
    }

    // compound flush policy, size or count
    // testPerfCompoundFlushPolicy();
}

void testPerfBufferedFile() {
    const char* cfg =
        "file:///./bench_data/"
        "elog_bench_buffered512.log?file_buffer_size=512bytes&file_lock=yes&flush_policy=none";
    runMultiThreadTest("Buffered File (512 bytes)", "elog_bench_buffered512", cfg);

    cfg =
        "file:///./bench_data/"
        "elog_bench_buffered4kb.log?file_buffer_size=4k&file_lock=yes&flush_policy=none";
    runMultiThreadTest("Buffered File (4kb)", "elog_bench_buffered4kb", cfg);

    cfg =
        "file:///./bench_data/"
        "elog_bench_buffered64kb.log?file_buffer_size=64k&file_lock=yes&flush_policy=none";
    runMultiThreadTest("Buffered File (64kb)", "elog_bench_buffered64kb", cfg);

    cfg =
        "file:///./bench_data/"
        "elog_bench_buffered1mb.log?file_buffer_size=1mb&file_lock=yes&flush_policy=none";
    runMultiThreadTest("Buffered File (1mb)", "elog_bench_buffered1mb", cfg);

    cfg =
        "file:///./bench_data/"
        "elog_bench_buffered4mb.log?file_buffer_size=4mb&file_lock=yes&flush_policy=none";
    runMultiThreadTest("Buffered File (4mb)", "elog_bench_buffered4mb", cfg);
}

void testPerfSegmentedFile() {
    const char* cfg =
        "file:///./bench_data/elog_bench_segmented_1mb.log?"
        "file_segment_size=1mb&file_buffer_size=64kb&flush_policy=none";
    runMultiThreadTest("Segmented File (1MB segment size)", "elog_bench_segmented_1mb", cfg);

    cfg =
        "file:///./bench_data/elog_bench_segmented_2mb.log?"
        "file_segment_size=2mb&file_buffer_size=64kb&flush_policy=none";
    runMultiThreadTest("Segmented File (2MB segment size)", "elog_bench_segmented_2mb", cfg);

    cfg =
        "file:///./bench_data/elog_bench_segmented_4mb.log?"
        "file_segment_size=4mb&file_buffer_size=64kb&flush_policy=none";
    runMultiThreadTest("Segmented File (4MB segment size)", "elog_bench_segmented_4mb", cfg);
}

void testPerfRotatingFile() {
    const char* cfg =
        "file:///./bench_data/elog_bench_rotating_1mb.log?"
        "file_segment_size=1mb&file_segment_count=5&"
        "file_buffer_size=64kb&"
        "flush_policy=none";
    runMultiThreadTest("Rotating File (1MB segment size)", "elog_bench_rotating_1mb", cfg);

    cfg =
        "file:///./bench_data/elog_bench_rotating_2mb.log?"
        "file_segment_size=2mb&file_segment_count=5&"
        "file_buffer_size=64kb&"
        "flush_policy=none";
    runMultiThreadTest("Rotating File (2MB segment size)", "elog_bench_rotating_2mb", cfg);

    cfg =
        "file:///./bench_data/elog_bench_rotating_4mb.log?"
        "file_segment_size=4mb&file_segment_count=5&"
        "file_buffer_size=64kb&"
        "flush_policy=none";
    runMultiThreadTest("Rotating File (4MB segment size)", "elog_bench_rotating_4mb", cfg);
}

void testPerfDeferredFile() {
    /*const char* cfg =
        "file://./bench_data/"
        "elog_bench_deferred.log?file_buffer_size=4194304&file_lock=no&flush_policy=count&flush-"
        "count=4096&deferred";*/
    /*const char* cfg =
        "file://./bench_data/"
        "elog_bench_deferred.log?flush_policy=count&flush_count=4096&deferred";*/
    const char* cfg =
        "async://deferred?flush_policy=count&flush_count=4096&name=elog_bench|"
        "file:///./bench_data/elog_bench_deferred.log?file_buffer_size=4kb&file_lock=no";
    cfg =
        "async://deferred?name=elog_bench|"
        "file:///./bench_data/elog_bench_deferred.log?file_buffer_size=1mb&file_lock=no";
    runMultiThreadTest("Deferred (1MB Buffer)", "elog_bench_deferred", cfg);
}

void testPerfQueuedFile() {
    /*const char* cfg =
        "file://./bench_data/"
        "elog_bench_queued.log?file_buffer_size=4194304&file_lock=no&flush_policy=count&flush-"
        "count=4096&queue_batch_size=10000&queue_"
        "timeout_millis=200";*/
    /*const char* cfg =
        "file://./bench_data/"
        "elog_bench_queued.log?flush_policy=count&flush_count=4096&queue_batch_size=10000&"
        "queue_"
        "timeout_millis=200";*/
    const char* cfg =
        "async://queued?queue_batch_size=10000&queue_timeout=200ms&"
        "flush_policy=count&flush_count=4096&name=elog_bench|"
        "file:///./bench_data/elog_bench_queued.log?file_buffer_size=4kb&file_lock=no";
    cfg =
        "async://queued?queue_batch_size=10000&queue_timeout=200ms&name=elog_bench|"
        "file:///./bench_data/elog_bench_queued.log?file_buffer_size=1mb&file_lock=no";
    runMultiThreadTest("Queued 100000 + 200ms (1MB Buffer)", "elog_bench_queued", cfg);
}

void testPerfQuantumFile(bool privateLogger) {
    /*const char* cfg =
        "file://./bench_data/"
        "elog_bench_quantum.log?file_buffer_size=4194304&file_lock=no&flush_policy=count&flush-"
        "count=4096&quantum_buffer_size=2000000";*/
    const char* cfg =
        "async://"
        "quantum?quantum_buffer_size=2000000&flush_policy=count&flush_count=4096&name=elog_"
        "bench"
        "|file:///./bench_data/elog_bench_quantum.log?file_buffer_size=4k&file_lock=no";
    cfg =
        "async://"
        "quantum?quantum_buffer_size=2000000&name=elog_bench"
        "|file:///./bench_data/elog_bench_quantum.log?file_buffer_size=1mb&file_lock=no";
    runMultiThreadTest("Quantum 2000000 (1MB Buffer)", "elog_bench_quantum", cfg, privateLogger);
}

static void testPerfMultiQuantumFile() {
    const char* cfg =
        "async://"
        "multi_quantum?quantum_buffer_size=11000&name=elog_bench"
        "|file:///./bench_data/elog_bench_multi_quantum.log?file_buffer_size=1mb&file_lock=no";
    runMultiThreadTest("Multi Quantum 11000 (1MB Buffer)", "elog_bench_multi_quantum", cfg);
}

#ifdef ELOG_ENABLE_FMT_LIB
void testPerfQuantumFileBinary() {
    const char* cfg =
        "async://"
        "quantum?quantum_buffer_size=2000000&name=elog_bench"
        "|file:///./bench_data/elog_bench_quantum_bin.log?file_buffer_size=1mb&file_lock=no";
    runMultiThreadTestBinary("Quantum 2000000 (1MB Buffer, Binary)", "elog_bench_quantum_bin", cfg,
                             true);
}

void testPerfQuantumFileBinaryCached() {
    const char* cfg =
        "async://"
        "quantum?quantum_buffer_size=2000000&name=elog_bench"
        "|file:///./bench_data/"
        "elog_bench_quantum_bin_cache.log?file_buffer_size=1mb&file_lock=no";
    runMultiThreadTestBinaryCached("Quantum 2000000 (1MB Buffer, Binary, Auto-Cached)",
                                   "elog_bench_quantum_bin_auto_cache", cfg, true);
}

void testPerfQuantumFileBinaryPreCached() {
    const char* cfg =
        "async://"
        "quantum?quantum_buffer_size=2000000&name=elog_bench"
        "|file:///./bench_data/"
        "elog_bench_quantum_bin_pre_cache.log?file_buffer_size=1mb&file_lock=no";
    runMultiThreadTestBinaryPreCached("Quantum 2000000 (1MB Buffer, Binary, Pre-Cached)",
                                      "elog_bench_quantum_bin_pre_cache", cfg, true);
}

void testPerfMultiQuantumFileBinary() {
    const char* cfg =
        "async://"
        "multi_quantum?quantum_buffer_size=11000&name=elog_bench"
        "|file:///./bench_data/"
        "elog_bench_multi_quantum_bin.log?file_buffer_size=1mb&file_lock=no";
    runMultiThreadTestBinary("Multi Quantum 11000 (1MB Buffer, Binary)",
                             "elog_bench_multi_quantum_bin", cfg);
}

void testPerfMultiQuantumFileBinaryCached() {
    const char* cfg =
        "async://"
        "multi_quantum?quantum_buffer_size=11000&name=elog_bench"
        "|file:///./bench_data/"
        "elog_bench_multi_quantum_bin_cache.log?file_buffer_size=1mb&file_lock=no";
    runMultiThreadTestBinaryCached("Multi Quantum 11000 (1MB Buffer, Binary, Auto-Cached)",
                                   "elog_bench_multi_quantum_bin_auto_cache", cfg);
}

void testPerfMultiQuantumFileBinaryPreCached() {
    const char* cfg =
        "async://"
        "multi_quantum?quantum_buffer_size=11000&name=elog_bench"
        "|file:///./bench_data/"
        "elog_bench_multi_quantum_bin_pre_cache.log?file_buffer_size=1mb&file_lock=no";
    runMultiThreadTestBinaryPreCached("Multi Quantum 11000 (1MB Buffer, Binary, Pre-Cached)",
                                      "elog_bench_multi_quantum_bin_pre_cache", cfg);
}
#endif

static void writeSTCsv(const char* fname, const std::vector<double>& data) {
    std::ofstream f(fname, std::ios_base::trunc);
    int column = 0;
    f << column << " \"Flush\\nImmediate\" " << std::fixed << std::setprecision(2) << data[column++]
      << std::endl;
    f << column << " \"Flush\\nNever\" " << std::fixed << std::setprecision(2) << data[column++]
      << std::endl;
    /*f << column << " \"Flush\\nGroup\" " << std::fixed << std::setprecision(2) <<
      data[column++]
      << std::endl;*/
    f << column << " \"Flush\\nCount=4096\" " << std::fixed << std::setprecision(2)
      << data[column++] << std::endl;
    f << column << " \"Flush\\nSize=1MB\" " << std::fixed << std::setprecision(2) << data[column++]
      << std::endl;
    f << column << " \"Flush\\nTime=200ms\" " << std::fixed << std::setprecision(2)
      << data[column++] << std::endl;
    f << column << " \"Buffered\\nSize=1MB\" " << std::fixed << std::setprecision(2)
      << data[column++] << std::endl;
    f << column << " \"Segmented\\nSize=15MB\" " << std::fixed << std::setprecision(2)
      << data[column++] << std::endl;
    f << column << " \"Rotating\\nSize=15MB\" " << std::fixed << std::setprecision(2)
      << data[column++] << std::endl;
    f << column << " Deferred " << std::fixed << std::setprecision(2) << data[column++]
      << std::endl;
    f << column << " Queued " << std::fixed << std::setprecision(2) << data[column++] << std::endl;
    f << column << " Quantum " << std::fixed << std::setprecision(2) << data[column++] << std::endl;
#ifdef ELOG_ENABLE_FMT_LIB
    f << column << " Quantum-Bin " << std::fixed << std::setprecision(2) << data[column++]
      << std::endl;
    f << column << " Quantum-Bin\\nAuto-Cache " << std::fixed << std::setprecision(2)
      << data[column++] << std::endl;
    f << column << " Quantum-Bin\\nPre-Cache " << std::fixed << std::setprecision(2)
      << data[column++] << std::endl;
#endif
    f.close();
}

void testPerfAllSingleThread() {
    std::vector<double> msgThroughput;
    std::vector<double> ioThroughput;
    std::vector<double> msgp50;
    std::vector<double> msgp95;
    std::vector<double> msgp99;

    if (sTestSingleAll || sTestSingleThreadFlushImmediate) {
        testPerfSTFlushImmediate(msgThroughput, ioThroughput, msgp50, msgp95, msgp99);
    }
    if (sTestSingleAll || sTestSingleThreadFlushNever) {
        testPerfSTFlushNever(msgThroughput, ioThroughput, msgp50, msgp95, msgp99);
    }
    /*if (sTestSingleAll || sTestSingleThreadFlushGroup) {
        testPerfSTFlushGroup(msgThroughput, ioThroughput, msgp50, msgp95, msgp99);
    }*/
    if (sTestSingleAll || sTestSingleThreadFlushCount) {
        testPerfSTFlushCount4096(msgThroughput, ioThroughput, msgp50, msgp95, msgp99);
    }
    if (sTestSingleAll || sTestSingleThreadFlushSize) {
        testPerfSTFlushSize1mb(msgThroughput, ioThroughput, msgp50, msgp95, msgp99);
    }
    if (sTestSingleAll || sTestSingleThreadFlushTime) {
        testPerfSTFlushTime200ms(msgThroughput, ioThroughput, msgp50, msgp95, msgp99);
    }
    if (sTestSingleAll || sTestSingleThreadBuffered) {
        testPerfSTBufferedFile1mb(msgThroughput, ioThroughput, msgp50, msgp95, msgp99);
    }
    if (sTestSingleAll || sTestSingleThreadSegmented) {
        testPerfSTSegmentedFile1mb(msgThroughput, ioThroughput, msgp50, msgp95, msgp99);
    }
    if (sTestSingleAll || sTestSingleThreadRotating) {
        testPerfSTRotatingFile1mb(msgThroughput, ioThroughput, msgp50, msgp95, msgp99);
    }
    if (sTestSingleAll || sTestSingleThreadDeferred) {
        testPerfSTDeferredCount4096(msgThroughput, ioThroughput, msgp50, msgp95, msgp99);
    }
    if (sTestSingleAll || sTestSingleThreadQueued) {
        testPerfSTQueuedCount4096(msgThroughput, ioThroughput, msgp50, msgp95, msgp99);
    }
    if (sTestSingleAll || sTestSingleThreadQuantum) {
        testPerfSTQuantumCount4096(msgThroughput, ioThroughput, msgp50, msgp95, msgp99);
    }
#ifdef ELOG_ENABLE_FMT_LIB
    if (sTestSingleAll || sTestSingleThreadQuantumBinary) {
        testPerfSTQuantumBinary(msgThroughput, ioThroughput, msgp50, msgp95, msgp99);
    }
    if (sTestSingleAll || sTestSingleThreadQuantumBinaryCached) {
        testPerfSTQuantumBinaryCached(msgThroughput, ioThroughput, msgp50, msgp95, msgp99);
    }
    if (sTestSingleAll || sTestSingleThreadQuantumBinaryPreCached) {
        testPerfSTQuantumBinaryPreCached(msgThroughput, ioThroughput, msgp50, msgp95, msgp99);
    }
#endif

    // now write CSV for drawing bar chart with gnuplot
    if (sTestSingleAll) {
        writeSTCsv("./bench_data/st_msg.csv", msgThroughput);
#ifdef MEASURE_PERCENTILE
        writeSTCsv("./bench_data/st_msg_p50.csv", msgp50);
        writeSTCsv("./bench_data/st_msg_p95.csv", msgp95);
        writeSTCsv("./bench_data/st_msg_p99.csv", msgp99);
#endif
    }
}

void testPerfSTFlushImmediate(std::vector<double>& msgThroughput, std::vector<double>& ioThroughput,
                              std::vector<double>& msgp50, std::vector<double>& msgp95,
                              std::vector<double>& msgp99) {
    const char* cfg =
        "file:///./bench_data/elog_bench_flush_immediate_st.log?flush_policy=immediate";
    double msgPerf = 0.0f;
    double ioPerf = 0.0f;
    StatData statData;
    runSingleThreadedTest("Flush Immediate", cfg, msgPerf, ioPerf, statData);
    msgThroughput.push_back(msgPerf);
    ioThroughput.push_back(ioPerf);
#ifdef MEASURE_PERCENTILE
    msgp50.push_back(statData.p50);
    msgp95.push_back(statData.p95);
    msgp99.push_back(statData.p99);
#endif
}

void testPerfSTFlushNever(std::vector<double>& msgThroughput, std::vector<double>& ioThroughput,
                          std::vector<double>& msgp50, std::vector<double>& msgp95,
                          std::vector<double>& msgp99) {
    const char* cfg = "file:///./bench_data/elog_bench_flush_never_st.log?flush_policy=never";
    double msgPerf = 0.0f;
    double ioPerf = 0.0f;
    StatData statData;
    runSingleThreadedTest("Flush Never", cfg, msgPerf, ioPerf, statData);
    msgThroughput.push_back(msgPerf);
    ioThroughput.push_back(ioPerf);
#ifdef MEASURE_PERCENTILE
    msgp50.push_back(statData.p50);
    msgp95.push_back(statData.p95);
    msgp99.push_back(statData.p99);
#endif
}

void testPerfSTFlushGroup(std::vector<double>& msgThroughput, std::vector<double>& ioThroughput,
                          std::vector<double>& msgp50, std::vector<double>& msgp95,
                          std::vector<double>& msgp99) {
    const char* cfg =
        "file:///./bench_data/elog_bench_flush_group_st.log?"
        "flush_policy=(CHAIN(immediate, group(size:4, timeout:200micros)))";
    double msgPerf = 0.0f;
    double ioPerf = 0.0f;
    StatData statData;
    runSingleThreadedTest("Flush Group", cfg, msgPerf, ioPerf, statData);
    msgThroughput.push_back(msgPerf);
    ioThroughput.push_back(ioPerf);
#ifdef MEASURE_PERCENTILE
    msgp50.push_back(statData.p50);
    msgp95.push_back(statData.p95);
    msgp99.push_back(statData.p99);
#endif
}

void testPerfSTFlushCount4096(std::vector<double>& msgThroughput, std::vector<double>& ioThroughput,
                              std::vector<double>& msgp50, std::vector<double>& msgp95,
                              std::vector<double>& msgp99) {
    const char* cfg =
        "file:///./bench_data/"
        "elog_bench_flush_count4096_st.log?flush_policy=count&flush_count=4096";
    double msgPerf = 0.0f;
    double ioPerf = 0.0f;
    StatData statData;
    runSingleThreadedTest("Flush Count=4096", cfg, msgPerf, ioPerf, statData);
    msgThroughput.push_back(msgPerf);
    ioThroughput.push_back(ioPerf);
#ifdef MEASURE_PERCENTILE
    msgp50.push_back(statData.p50);
    msgp95.push_back(statData.p95);
    msgp99.push_back(statData.p99);
#endif
}

void testPerfSTFlushSize1mb(std::vector<double>& msgThroughput, std::vector<double>& ioThroughput,
                            std::vector<double>& msgp50, std::vector<double>& msgp95,
                            std::vector<double>& msgp99) {
    const char* cfg =
        "file:///./bench_data/"
        "elog_bench_flush_size_1mb_st.log?flush_policy=size&flush_size=1mb";
    double msgPerf = 0.0f;
    double ioPerf = 0.0f;
    StatData statData;
    runSingleThreadedTest("Flush Size=1MB", cfg, msgPerf, ioPerf, statData);
    msgThroughput.push_back(msgPerf);
    ioThroughput.push_back(ioPerf);
#ifdef MEASURE_PERCENTILE
    msgp50.push_back(statData.p50);
    msgp95.push_back(statData.p95);
    msgp99.push_back(statData.p99);
#endif
}

void testPerfSTFlushTime200ms(std::vector<double>& msgThroughput, std::vector<double>& ioThroughput,
                              std::vector<double>& msgp50, std::vector<double>& msgp95,
                              std::vector<double>& msgp99) {
    const char* cfg =
        "file:///./bench_data/"
        "elog_bench_flush_time_200ms_st.log?flush_policy=time&flush_timeout=200ms";
    double msgPerf = 0.0f;
    double ioPerf = 0.0f;
    StatData statData;
    runSingleThreadedTest("Flush Time=200ms", cfg, msgPerf, ioPerf, statData);
    msgThroughput.push_back(msgPerf);
    ioThroughput.push_back(ioPerf);
#ifdef MEASURE_PERCENTILE
    msgp50.push_back(statData.p50);
    msgp95.push_back(statData.p95);
    msgp99.push_back(statData.p99);
#endif
}

void testPerfSTBufferedFile1mb(std::vector<double>& msgThroughput,
                               std::vector<double>& ioThroughput, std::vector<double>& msgp50,
                               std::vector<double>& msgp95, std::vector<double>& msgp99) {
    const char* cfg =
        "file:///./bench_data/"
        "elog_bench_buffered_1mb_st.log?file_buffer_size=1mb&flush_policy=none";
    double msgPerf = 0.0f;
    double ioPerf = 0.0f;
    StatData statData;
    runSingleThreadedTest("Buffered Size=1mb", cfg, msgPerf, ioPerf, statData);
    msgThroughput.push_back(msgPerf);
    ioThroughput.push_back(ioPerf);
#ifdef MEASURE_PERCENTILE
    msgp50.push_back(statData.p50);
    msgp95.push_back(statData.p95);
    msgp99.push_back(statData.p99);
#endif
}

void testPerfSTSegmentedFile1mb(std::vector<double>& msgThroughput,
                                std::vector<double>& ioThroughput, std::vector<double>& msgp50,
                                std::vector<double>& msgp95, std::vector<double>& msgp99) {
    // segmentation takes place at ~13000 messages with 1 mb segment, meaning that with 1000000
    // messages we get ~76 segments, this is too much for a short test
    // we would like to restrict the segment count to let's say 5 so we use segment size 15 mb
    const char* cfg =
        "file:///./bench_data/"
        "elog_bench_segmented_15mb_st.log?file_segment_size=15mb&file_buffer_size=1mb&"
        "flush_policy=none";
    double msgPerf = 0.0f;
    double ioPerf = 0.0f;
    StatData statData;
    runSingleThreadedTest("Segmented Size=15mb", cfg, msgPerf, ioPerf, statData);
    msgThroughput.push_back(msgPerf);
    ioThroughput.push_back(ioPerf);
#ifdef MEASURE_PERCENTILE
    msgp50.push_back(statData.p50);
    msgp95.push_back(statData.p95);
    msgp99.push_back(statData.p99);
#endif
}

void testPerfSTRotatingFile1mb(std::vector<double>& msgThroughput,
                               std::vector<double>& ioThroughput, std::vector<double>& msgp50,
                               std::vector<double>& msgp95, std::vector<double>& msgp99) {
    // rotation takes place at ~13000 messages with 1 mb segment, meaning that with 1000000
    // messages we get ~76 segment rotation, this is too much for a short test we would like to
    // restrict the segment count to let's say 5 so we use segment size 15 mb
    const char* cfg =
        "file:///./bench_data/"
        "elog_bench_rotating_15mb.log?file_segment_size=15mb&file_buffer_size=1mb&"
        "file_segment_count=5&flush_policy=none";
    double msgPerf = 0.0f;
    double ioPerf = 0.0f;
    StatData statData;
    // we send 10000 messages to avoid rotation
    runSingleThreadedTest("Rotating Size=15mb", cfg, msgPerf, ioPerf, statData);
    msgThroughput.push_back(msgPerf);
    ioThroughput.push_back(ioPerf);
#ifdef MEASURE_PERCENTILE
    msgp50.push_back(statData.p50);
    msgp95.push_back(statData.p95);
    msgp99.push_back(statData.p99);
#endif
}

void testPerfSTDeferredCount4096(std::vector<double>& msgThroughput,
                                 std::vector<double>& ioThroughput, std::vector<double>& msgp50,
                                 std::vector<double>& msgp95, std::vector<double>& msgp99) {
    /*const char* cfg =
        "file://./bench_data/"
        "elog_bench_deferred_st.log?file_buffer_size=4194304&file_lock=no&flush_policy=count&flush-"
        "count=4096&deferred";*/
    const char* cfg =
        "async://deferred?flush_policy=count&flush_count=4096&name=elog_bench|"
        "file:///./bench_data/elog_bench_deferred_st.log";
    double msgPerf = 0.0f;
    double ioPerf = 0.0f;
    StatData statData;
    runSingleThreadedTest("Deferred", cfg, msgPerf, ioPerf, statData);
    msgThroughput.push_back(msgPerf);
    ioThroughput.push_back(ioPerf);
#ifdef MEASURE_PERCENTILE
    msgp50.push_back(statData.p50);
    msgp95.push_back(statData.p95);
    msgp99.push_back(statData.p99);
#endif
}

void testPerfSTQueuedCount4096(std::vector<double>& msgThroughput,
                               std::vector<double>& ioThroughput, std::vector<double>& msgp50,
                               std::vector<double>& msgp95, std::vector<double>& msgp99) {
    /*const char* cfg =
        "file://./bench_data/"
        "elog_bench_queued_st.log?file_buffer_size=4194304&file_lock=no&flush_policy=count&flush-"
        "count=4096&queue_batch_size=10000&queue_"
        "timeout_millis=500";*/
    const char* cfg =
        "async://queued?queue_batch_size=10000&queue_timeout=500ms&"
        "flush_policy=count&flush_count=4096&name=elog_bench|"
        "file:///./bench_data/elog_bench_queued_st.log";
    double msgPerf = 0.0f;
    double ioPerf = 0.0f;
    StatData statData;
    runSingleThreadedTest("Queued", cfg, msgPerf, ioPerf, statData);
    msgThroughput.push_back(msgPerf);
    ioThroughput.push_back(ioPerf);
#ifdef MEASURE_PERCENTILE
    msgp50.push_back(statData.p50);
    msgp95.push_back(statData.p95);
    msgp99.push_back(statData.p99);
#endif
}

void testPerfSTQuantumCount4096(std::vector<double>& msgThroughput,
                                std::vector<double>& ioThroughput, std::vector<double>& msgp50,
                                std::vector<double>& msgp95, std::vector<double>& msgp99) {
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
        "{"
        "   log_target = {"
        "       scheme = async,"
        "       type = quantum,"
        "       quantum_buffer_size = 2000000,"
        "       name = elog_bench,"
        "       log_target = {"
        "           scheme = file,"
        "           path = ./bench_data/elog_bench_quantum_st.log,"
        "           flush_policy = {"
        "               type = count,"
        "               flush_count = 4096"
        "           },"
        "           file_buffer_size = 4kb,"
        "           file_lock = no"
        "       }"
        "   }"
        "}";
    /*const char* cfg =
        "file://./bench_data/elog_bench_quantum.log?file_buffer_size=4096&file_lock=no&"
        //"flush_policy=count&flush_count=4096&"
        "quantum_buffer_size=2000000";*/
    cfg =
        "async://"
        "quantum?quantum_buffer_size=2000000&name=elog_bench"
        "|file:///./bench_data/elog_bench_quantum_st.log?file_buffer_size=1mb&file_lock=no";
    double msgPerf = 0.0f;
    double ioPerf = 0.0f;
    StatData statData;
    runSingleThreadedTest("Quantum", cfg, msgPerf, ioPerf, statData);
    msgThroughput.push_back(msgPerf);
    ioThroughput.push_back(ioPerf);
#ifdef MEASURE_PERCENTILE
    msgp50.push_back(statData.p50);
    msgp95.push_back(statData.p95);
    msgp99.push_back(statData.p99);
#endif
}

#ifdef ELOG_ENABLE_FMT_LIB
void testPerfSTQuantumBinary(std::vector<double>& msgThroughput, std::vector<double>& ioThroughput,
                             std::vector<double>& msgp50, std::vector<double>& msgp95,
                             std::vector<double>& msgp99) {
    const char* cfg =
        "async://"
        "quantum?quantum_buffer_size=2000000&name=elog_bench"
        "|file:///./bench_data/elog_bench_quantum_bin_st.log?file_buffer_size=1mb&file_lock=no";
    double msgPerf = 0.0f;
    double ioPerf = 0.0f;
    StatData statData;
    runSingleThreadedTestBinary("Quantum Binary", cfg, msgPerf, ioPerf, statData);
    msgThroughput.push_back(msgPerf);
    ioThroughput.push_back(ioPerf);
#ifdef MEASURE_PERCENTILE
    msgp50.push_back(statData.p50);
    msgp95.push_back(statData.p95);
    msgp99.push_back(statData.p99);
#endif
}

void testPerfSTQuantumBinaryCached(std::vector<double>& msgThroughput,
                                   std::vector<double>& ioThroughput, std::vector<double>& msgp50,
                                   std::vector<double>& msgp95, std::vector<double>& msgp99) {
    const char* cfg =
        "async://"
        "quantum?quantum_buffer_size=2000000&name=elog_bench"
        "|file:///./bench_data/"
        "elog_bench_quantum_bin_cache_st.log?file_buffer_size=1mb&file_lock=no";
    double msgPerf = 0.0f;
    double ioPerf = 0.0f;
    StatData statData;
    runSingleThreadedTestBinaryCached("Quantum Binary Cached", cfg, msgPerf, ioPerf, statData);
    msgThroughput.push_back(msgPerf);
    ioThroughput.push_back(ioPerf);
#ifdef MEASURE_PERCENTILE
    msgp50.push_back(statData.p50);
    msgp95.push_back(statData.p95);
    msgp99.push_back(statData.p99);
#endif
}

void testPerfSTQuantumBinaryPreCached(std::vector<double>& msgThroughput,
                                      std::vector<double>& ioThroughput,
                                      std::vector<double>& msgp50, std::vector<double>& msgp95,
                                      std::vector<double>& msgp99) {
    const char* cfg =
        "async://"
        "quantum?quantum_buffer_size=2000000&name=elog_bench"
        "|file:///./bench_data/"
        "elog_bench_quantum_bin_pre_cache_st.log?file_buffer_size=1mb&file_lock=no";
    double msgPerf = 0.0f;
    double ioPerf = 0.0f;
    StatData statData;
    runSingleThreadedTestBinaryPreCached("Quantum Binary Pre-Cached", cfg, msgPerf, ioPerf,
                                         statData);
    msgThroughput.push_back(msgPerf);
    ioThroughput.push_back(ioPerf);
#ifdef MEASURE_PERCENTILE
    msgp50.push_back(statData.p50);
    msgp95.push_back(statData.p95);
    msgp99.push_back(statData.p99);
#endif
}
#endif

void testPerfFileNeverFlushPolicy() {
    const char* cfg = "file:///./bench_data/elog_bench_flush_never.log?flush_policy=never";
    runMultiThreadTest("File (Never Flush Policy)", "elog_bench_flush_never", cfg);
}

static void testPerfImmediateFlushPolicy() {
    const char* cfg = "file:///./bench_data/elog_bench_flush_immediate.log?flush_policy=immediate";
    runMultiThreadTest("File (Immediate Flush Policy)", "elog_bench_flush_immediate", cfg);
}

static void testPerfGroupFlushPolicy() {
    if (sGroupSize != 0 && sGroupTimeoutMicros != 0) {
        char cfg[1024];
        snprintf(cfg, 1024,
                 "file:///./bench_data/elog_bench_group_%d_%dms.log?"
                 "flush_policy=(CHAIN(immediate, group(size:%d, timeout:%dmicros)))",
                 sGroupSize, sGroupTimeoutMicros, sGroupSize, sGroupTimeoutMicros);
        runMultiThreadTest("Group File (Custom)", "elog_bench_group_custom", cfg, true, sGroupSize);
        return;
    }
    // sMsgCnt = 100;
    const char* cfg =
        "file:///./bench_data/elog_bench_group_4_100ms.log?"
        "flush_policy=(CHAIN(immediate, group(size:4, timeout:100micros)))";
    runMultiThreadTest("Group File (4/100)", "elog_bench_group_4_100ms", cfg, true, 4);
    cfg =
        "file:///./bench_data/elog_bench_group_4_200ms.log?"
        "flush_policy=(CHAIN(immediate, group(size:4, timeout:200micros)))";
    runMultiThreadTest("Group File (4/200)", "elog_bench_group_4_200ms", cfg, true, 4);

    cfg =
        "file:///./bench_data/elog_bench_group_4_500ms.log?"
        "flush_policy=(CHAIN(immediate, group(size:4, timeout:500micros)))";
    runMultiThreadTest("Group File (4/500)", "elog_bench_group_4_500ms", cfg, true, 4);

    cfg =
        "file:///./bench_data/elog_bench_group_4_1000ms.log?"
        "flush_policy=(CHAIN(immediate, group(size:4, timeout:1000micros)))";
    runMultiThreadTest("Group File (4/1000)", "elog_bench_group_4_1000ms", cfg, true, 4);

    cfg =
        "file:///./bench_data/elog_bench_group_8_100ms.log?"
        "flush_policy=(CHAIN(immediate, group(size:8, timeout:100micros)))";
    runMultiThreadTest("Group File (8/100)", "elog_bench_group_8_100ms", cfg, true, 8);

    cfg =
        "file:///./bench_data/"
        "elog_bench_group_8_200ms.log?flush_policy=(CHAIN(immediate, group(size:8, "
        "timeout:200micros)))";
    runMultiThreadTest("Group File (8/200)", "elog_bench_group_8_200ms", cfg, true, 8);

    cfg =
        "file:///./bench_data/elog_bench_group_8_500ms.log?"
        "flush_policy=(CHAIN(immediate, group(size:8, timeout:500micros)))";
    runMultiThreadTest("Group File (8/500)", "elog_bench_group_8_500ms", cfg, true, 8);
}

static void testPerfCountFlushPolicy() {
    const char* cfg =
        "file:///./bench_data/elog_bench_count64.log?flush_policy=count&flush_count=64";
    runMultiThreadTest("File (Count 64 Flush Policy)", "elog_bench_count64", cfg);

    cfg = "file:///./bench_data/elog_bench_count256.log?flush_policy=count&flush_count=256";
    runMultiThreadTest("File (Count 256 Flush Policy)", "elog_bench_count256", cfg);

    cfg = "file:///./bench_data/elog_bench_count512.log?flush_policy=count&flush_count=512";
    runMultiThreadTest("File (Count 512 Flush Policy)", "elog_bench_count512", cfg);

    cfg = "file:///./bench_data/elog_bench_count1024.log?flush_policy=count&flush_count=1024";
    runMultiThreadTest("File (Count 1024 Flush Policy)", "elog_bench_count1024", cfg);

    cfg = "file:///./bench_data/elog_bench_count4096.log?flush_policy=count&flush_count=4096";
    runMultiThreadTest("File (Count 4096 Flush Policy)", "elog_bench_count4096", cfg);
}

static void testPerfSizeFlushPolicy() {
    const char* cfg =
        "file:///./bench_data/elog_bench_size64.log?flush_policy=size&flush_size=64bytes";
    runMultiThreadTest("File (Size 64 bytes Flush Policy)", "elog_bench_size64", cfg);

    cfg = "file:///./bench_data/elog_bench_size_1kb.log?flush_policy=size&flush_size=1kb";
    runMultiThreadTest("File (Size 1KB Flush Policy)", "elog_bench_size_1kb", cfg);

    cfg = "file:///./bench_data/elog_bench_size_4kb.log?flush_policy=size&flush_size=4kb";
    runMultiThreadTest("File (Size 4KB Flush Policy)", "elog_bench_size_4kb", cfg);

    cfg = "file:///./bench_data/elog_bench_size_64kb.log?flush_policy=size&flush_size=64kb";
    runMultiThreadTest("File (Size 64KB Flush Policy)", "elog_bench_size_64kb", cfg);

    cfg =
        "file:///./bench_data/"
        "elog_bench_size_1mb.log?flush_policy=size&flush_size=1mb";
    runMultiThreadTest("File (Size 1MB Flush Policy)", "elog_bench_size_1mb", cfg);
}

static void testPerfTimeFlushPolicy() {
    /*const char* cfg =
        "file://./bench_data/elog_bench_time_100ms.log?flush_policy=time&flush_timeout_millis=100";*/
    const char* cfg =
        "{ scheme = file, "
        "   path = ./bench_data/elog_bench_time_100ms.log, "
        "   flush_policy = time, "
        "   flush_timeout = 100ms, "
        "   name = elog_bench"
        "}";
    cfg =
        "file:///./bench_data/"
        "elog_bench_time_100ms.log?flush_policy=time&flush_timeout=100ms";
    runMultiThreadTest("File (Time 100 ms Flush Policy)", "elog_bench_time_100ms", cfg);

    cfg =
        "file:///./bench_data/"
        "elog_bench_time_200ms.log?flush_policy=time&flush_timeout=200ms";
    runMultiThreadTest("File (Time 200 ms Flush Policy)", "elog_bench_time_200ms", cfg);

    cfg =
        "file:///./bench_data/"
        "elog_bench_time_500ms.log?flush_policy=time&flush_timeout=500ms";
    runMultiThreadTest("File (Time 500 ms Flush Policy)", "elog_bench_time_500ms", cfg);

    cfg =
        "file:///./bench_data/"
        "elog_bench_time_1000ms.log?flush_policy=time&flush_timeout=1000ms";
    runMultiThreadTest("File (Time 1000 ms Flush Policy)", "elog_bench_time_1000ms", cfg);
}

static void testPerfCompoundFlushPolicy() {
    const char* cfg =
        "{ scheme = file, "
        "   path = ./bench_data/elog_bench_compound.log, "
        "   flush_policy = or, "
        "   flush_policy_args = ["
        "       { flush_policy = count, flush_count = 4096 },"
        "       { flush_policy = size, flush_size = 1kb },"
        "       { flush_policy = time, flush_timeout = 200ms }"
        "   ],"
        "   name = elog_bench"
        "}";
    runMultiThreadTest("File (Compound Flush Policy)", "elog_bench_compound", cfg);
}

#ifdef ELOG_ENABLE_FMT_LIB
enum LogType { LT_NORMAL, LT_FMT, LT_BIN, LT_BIN_CACHE, LT_BIN_PRE_CACHE };

struct Param0 {
    LogType m_type;
    elog::ELogCacheEntryId m_id;
    Param0(LogType type) : m_type(type) {}
    inline void prep() { m_id = elog::getOrCacheFormatMsg("Single thread Test log"); }
    inline void run(elog::ELogLogger* logger, uint32_t msgCount) {
        for (uint32_t i = 0; i < msgCount; ++i) {
            switch (m_type) {
                case LT_NORMAL:
                    ELOG_INFO_EX(logger, "Single thread Test log");
                    break;
                case LT_FMT:
                    ELOG_FMT_INFO_EX(logger, "Single thread Test log");
                    break;
                case LT_BIN:
                    ELOG_BIN_INFO_EX(logger, "Single thread Test log");
                    break;
                case LT_BIN_CACHE:
                    ELOG_CACHE_INFO_EX(logger, "Single thread Test log");
                    break;
                case LT_BIN_PRE_CACHE:
                    ELOG_ID_INFO_EX(logger, m_id);
                    break;
            }
        }
    }
};

struct Param1Printf {
    LogType m_type;
    elog::ELogCacheEntryId m_id;
    Param1Printf(LogType type) : m_type(type) {}
    inline void run(elog::ELogLogger* logger, uint32_t msgCount) {
        for (uint32_t i = 0; i < msgCount; ++i) {
            ELOG_INFO_EX(logger, "Single thread Test log %u", i);
        }
    }
};

struct Param1 {
    LogType m_type;
    elog::ELogCacheEntryId m_id;
    Param1(LogType type) : m_type(type) {}
    inline void prep() { m_id = elog::getOrCacheFormatMsg("Single thread Test log {}"); }
    inline void run(elog::ELogLogger* logger, uint32_t msgCount) {
        for (uint32_t i = 0; i < msgCount; ++i) {
            switch (m_type) {
                case LT_NORMAL:
                    ELOG_INFO_EX(logger, "Single thread Test log %u", i);
                    break;
                case LT_FMT:
                    ELOG_FMT_INFO_EX(logger, "Single thread Test log {}", i);
                    break;
                case LT_BIN:
                    ELOG_BIN_INFO_EX(logger, "Single thread Test log {}", i);
                    break;
                case LT_BIN_CACHE:
                    ELOG_CACHE_INFO_EX(logger, "Single thread Test log {}", i);
                    break;
                case LT_BIN_PRE_CACHE:
                    ELOG_ID_INFO_EX(logger, m_id, i);
                    break;
            }
        }
    }
};

struct Param2 {
    LogType m_type;
    elog::ELogCacheEntryId m_id;
    Param2(LogType type) : m_type(type) {}
    inline void prep() { m_id = elog::getOrCacheFormatMsg("Single thread Test log {} {}"); }
    inline void run(elog::ELogLogger* logger, uint32_t msgCount) {
        for (uint32_t i = 0; i < msgCount; ++i) {
            switch (m_type) {
                case LT_NORMAL:
                    ELOG_INFO_EX(logger, "Single thread Test log %u %u", i, i);
                    break;
                case LT_FMT:
                    ELOG_FMT_INFO_EX(logger, "Single thread Test log {} {}", i, i);
                    break;
                case LT_BIN:
                    ELOG_BIN_INFO_EX(logger, "Single thread Test log {} {}", i, i);
                    break;
                case LT_BIN_CACHE:
                    ELOG_CACHE_INFO_EX(logger, "Single thread Test log {} {}", i, i);
                    break;
                case LT_BIN_PRE_CACHE:
                    ELOG_ID_INFO_EX(logger, m_id, i, i);
                    break;
            }
        }
    }
};

struct Param3 {
    LogType m_type;
    elog::ELogCacheEntryId m_id;
    Param3(LogType type) : m_type(type) {}
    inline void prep() { m_id = elog::getOrCacheFormatMsg("Single thread Test log {} {} {}"); }
    inline void run(elog::ELogLogger* logger, uint32_t msgCount) {
        for (uint32_t i = 0; i < msgCount; ++i) {
            switch (m_type) {
                case LT_NORMAL:
                    ELOG_INFO_EX(logger, "Single thread Test log %u %u %u", i, i, i);
                    break;
                case LT_FMT:
                    ELOG_FMT_INFO_EX(logger, "Single thread Test log {} {} {}", i, i, i);
                    break;
                case LT_BIN:
                    ELOG_BIN_INFO_EX(logger, "Single thread Test log {} {} {}", i, i, i);
                    break;
                case LT_BIN_CACHE:
                    ELOG_CACHE_INFO_EX(logger, "Single thread Test log {} {} {}", i, i, i);
                    break;
                case LT_BIN_PRE_CACHE:
                    ELOG_ID_INFO_EX(logger, m_id, i, i, i);
                    break;
            }
        }
    }
};

struct Param4 {
    LogType m_type;
    elog::ELogCacheEntryId m_id;
    Param4(LogType type) : m_type(type) {}
    inline void prep() { m_id = elog::getOrCacheFormatMsg("Single thread Test log {} {} {} {}"); }
    inline void run(elog::ELogLogger* logger, uint32_t msgCount) {
        for (uint32_t i = 0; i < msgCount; ++i) {
            switch (m_type) {
                case LT_NORMAL:
                    ELOG_INFO_EX(logger, "Single thread Test log %u %u %u %u", i, i, i, i);
                    break;
                case LT_FMT:
                    ELOG_FMT_INFO_EX(logger, "Single thread Test log {} {} {} {}", i, i, i, i);
                    break;
                case LT_BIN:
                    ELOG_BIN_INFO_EX(logger, "Single thread Test log {} {} {} {}", i, i, i, i);
                    break;
                case LT_BIN_CACHE:
                    ELOG_CACHE_INFO_EX(logger, "Single thread Test log {} {} {} {}", i, i, i, i);
                    break;
                case LT_BIN_PRE_CACHE:
                    ELOG_ID_INFO_EX(logger, m_id, i, i, i, i);
                    break;
            }
        }
    }
};

struct Param5 {
    LogType m_type;
    elog::ELogCacheEntryId m_id;
    Param5(LogType type) : m_type(type) {}
    inline void prep() {
        m_id = elog::getOrCacheFormatMsg("Single thread Test log {} {} {} {} {}");
    }
    inline void run(elog::ELogLogger* logger, uint32_t msgCount) {
        for (uint32_t i = 0; i < msgCount; ++i) {
            switch (m_type) {
                case LT_NORMAL:
                    ELOG_INFO_EX(logger, "Single thread Test log %u %u %u %u %u", i, i, i, i, i);
                    break;
                case LT_FMT:
                    ELOG_FMT_INFO_EX(logger, "Single thread Test log {} {} {} {} {}", i, i, i, i,
                                     i);
                    break;
                case LT_BIN:
                    ELOG_BIN_INFO_EX(logger, "Single thread Test log {} {} {} {} {}", i, i, i, i,
                                     i);
                    break;
                case LT_BIN_CACHE:
                    ELOG_CACHE_INFO_EX(logger, "Single thread Test log {} {} {} {} {}", i, i, i, i,
                                       i);
                    break;
                case LT_BIN_PRE_CACHE:
                    ELOG_ID_INFO_EX(logger, m_id, i, i, i, i, i);
                    break;
            }
        }
    }
};

struct Param6 {
    LogType m_type;
    elog::ELogCacheEntryId m_id;
    Param6(LogType type) : m_type(type) {}
    inline void prep() {
        m_id = elog::getOrCacheFormatMsg("Single thread Test log {} {} {} {} {} {}");
    }
    inline void run(elog::ELogLogger* logger, uint32_t msgCount) {
        for (uint32_t i = 0; i < msgCount; ++i) {
            switch (m_type) {
                case LT_NORMAL:
                    ELOG_INFO_EX(logger, "Single thread Test log %u %u %u %u %u %u", i, i, i, i, i,
                                 i);
                    break;
                case LT_FMT:
                    ELOG_FMT_INFO_EX(logger, "Single thread Test log {} {} {} {} {} {}", i, i, i, i,
                                     i, i);
                    break;
                case LT_BIN:
                    ELOG_BIN_INFO_EX(logger, "Single thread Test log {} {} {} {} {} {}", i, i, i, i,
                                     i, i);
                    break;
                case LT_BIN_CACHE:
                    ELOG_CACHE_INFO_EX(logger, "Single thread Test log {} {} {} {} {} {}", i, i, i,
                                       i, i, i);
                    break;
                case LT_BIN_PRE_CACHE:
                    ELOG_ID_INFO_EX(logger, m_id, i, i, i, i, i, i);
                    break;
            }
        }
    }
};

struct Param7 {
    LogType m_type;
    elog::ELogCacheEntryId m_id;
    Param7(LogType type) : m_type(type) {}
    inline void prep() {
        m_id = elog::getOrCacheFormatMsg("Single thread Test log {} {} {} {} {} {} {}");
    }
    inline void run(elog::ELogLogger* logger, uint32_t msgCount) {
        for (uint32_t i = 0; i < msgCount; ++i) {
            switch (m_type) {
                case LT_NORMAL:
                    ELOG_INFO_EX(logger, "Single thread Test log %u %u %u %u %u %u %u", i, i, i, i,
                                 i, i, i);
                    break;
                case LT_FMT:
                    ELOG_FMT_INFO_EX(logger, "Single thread Test log {} {} {} {} {} {} {}", i, i, i,
                                     i, i, i, i);
                    break;
                case LT_BIN:
                    ELOG_BIN_INFO_EX(logger, "Single thread Test log {} {} {} {} {} {} {}", i, i, i,
                                     i, i, i, i);
                    break;
                case LT_BIN_CACHE:
                    ELOG_CACHE_INFO_EX(logger, "Single thread Test log {} {} {} {} {} {} {}", i, i,
                                       i, i, i, i, i);
                    break;
                case LT_BIN_PRE_CACHE:
                    ELOG_ID_INFO_EX(logger, m_id, i, i, i, i, i, i, i);
                    break;
            }
        }
    }
};

struct Param8 {
    LogType m_type;
    elog::ELogCacheEntryId m_id;
    Param8(LogType type) : m_type(type) {}
    inline void prep() {
        m_id = elog::getOrCacheFormatMsg("Single thread Test log {} {} {} {} {} {} {} {}");
    }
    inline void run(elog::ELogLogger* logger, uint32_t msgCount) {
        for (uint32_t i = 0; i < msgCount; ++i) {
            switch (m_type) {
                case LT_NORMAL:
                    ELOG_INFO_EX(logger, "Single thread Test log %u %u %u %u %u %u %u %u", i, i, i,
                                 i, i, i, i, i);
                    break;
                case LT_FMT:
                    ELOG_FMT_INFO_EX(logger, "Single thread Test log {} {} {} {} {} {} {} {}", i, i,
                                     i, i, i, i, i, i);
                    break;
                case LT_BIN:
                    ELOG_BIN_INFO_EX(logger, "Single thread Test log {} {} {} {} {} {} {} {}", i, i,
                                     i, i, i, i, i, i);
                    break;
                case LT_BIN_CACHE:
                    ELOG_CACHE_INFO_EX(logger, "Single thread Test log {} {} {} {} {} {} {} {}", i,
                                       i, i, i, i, i, i, i);
                    break;
                case LT_BIN_PRE_CACHE:
                    ELOG_ID_INFO_EX(logger, m_id, i, i, i, i, i, i, i, i);
                    break;
            }
        }
    }
};

template <typename TestCode>
static void runBinaryAccelTest(const char* title, const char* cfg, TestCode& testCode,
                               double& msgThroughput, uint32_t msgCount = ST_MSG_COUNT,
                               bool enableTrace = false) {
    if (sMsgCnt > 0) {
        msgCount = sMsgCnt;
    }
    elog::ELogTarget* logTarget = initElog(cfg);
    if (logTarget == nullptr) {
        fprintf(stderr, "Failed to init %s test, aborting\n", title);
        return;
    }

    if (enableTrace) {
        elog::setReportLevel(elog::ELEVEL_TRACE);
    }

    fprintf(stderr, "\nRunning %s binary acceleration test\n", title);
    elog::ELogSource* logSource = elog::defineLogSource("elog.bench", true);
    elog::ELogLogger* logger = logSource->createPrivateLogger();

    // execute test
    testCode.prep();
    uint64_t bytesStart = logTarget->getBytesWritten();
    auto start = std::chrono::high_resolution_clock::now();
    testCode.run(logger, msgCount);
    auto end0 = std::chrono::high_resolution_clock::now();
    fprintf(stderr, "Finished logging, waiting for logger to catch up\n");

    while (!isCaughtUp(logTarget, msgCount)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(0));
    }
    auto end = std::chrono::high_resolution_clock::now();
    uint64_t bytesEnd = logTarget->getBytesWritten();
    std::chrono::microseconds testTime0 =
        std::chrono::duration_cast<std::chrono::microseconds>(end0 - start);
    std::chrono::microseconds testTime =
        std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    // print test result
    msgThroughput = msgCount / (double)testTime0.count() * 1000000.0f;
    fprintf(stderr, "Throughput: %0.3f MSg/Sec\n", msgThroughput);

    termELog();
}

static void writeAccelCsvFile(const char* testName, const char* baseName,
                              std::vector<double>& msgThroughput, uint32_t* tics = nullptr) {
    std::string fname =
        std::string("./bench_data/elog_bench_bin_accel_") + testName + "_" + baseName + "_msg.csv";
    std::ofstream f(fname, std::ios_base::trunc);

    // print in CSV format for gnuplot
    for (uint32_t i = 0; i < msgThroughput.size(); ++i) {
        f << ((tics == nullptr) ? i : tics[i]) << ", " << std::fixed << std::setprecision(2)
          << msgThroughput[i] << std::endl;
    }
    f.close();
}

void testPerfParamCount() {
    // test single-threaded for normal, binary, cached, pre-cached over quantum log target:
    // 0-8 int params, normal size log format message
    // varying message length, 1 parameter
    const char* cfg =
        "async://quantum?quantum_buffer_size=2000000&name=elog_bench"
        "|file:///./bench_data/"
        "elog_bench_quantum_accel_baseline.log?file_buffer_size=1mb&file_lock=no";
    std::vector<double> normalThroughput;
    std::vector<double> fmtThroughput;
    std::vector<double> binThroughput;
    std::vector<double> binCacheThroughput;
    std::vector<double> binPreCacheThroughput;

    double msgPerf = 0.0f;

#define RUN_TEST(id, type, resArray)                                                            \
    {                                                                                           \
        Param##id param##id(type);                                                              \
        runBinaryAccelTest("Binary Acceleration Param" #id " " #type, cfg, param##id, msgPerf); \
        resArray.push_back(msgPerf);                                                            \
    }

#define RUN_TEST_SET(type, resArray) \
    RUN_TEST(0, type, resArray);     \
    RUN_TEST(1, type, resArray);     \
    RUN_TEST(2, type, resArray);     \
    RUN_TEST(3, type, resArray);     \
    RUN_TEST(4, type, resArray);     \
    RUN_TEST(5, type, resArray);     \
    RUN_TEST(6, type, resArray);     \
    RUN_TEST(7, type, resArray);     \
    RUN_TEST(8, type, resArray);

    RUN_TEST_SET(LT_NORMAL, normalThroughput);
    RUN_TEST_SET(LT_FMT, fmtThroughput);
    RUN_TEST_SET(LT_BIN, binThroughput);
    RUN_TEST_SET(LT_BIN_CACHE, binCacheThroughput);
    RUN_TEST_SET(LT_BIN_PRE_CACHE, binPreCacheThroughput);

    // write results
    writeAccelCsvFile("param_count", "normal", normalThroughput);
    writeAccelCsvFile("param_count", "fmt", fmtThroughput);
    writeAccelCsvFile("param_count", "bin", binThroughput);
    writeAccelCsvFile("param_count", "bin_cache", binCacheThroughput);
    writeAccelCsvFile("param_count", "bin_pre_cache", binPreCacheThroughput);
}

struct MsgTest50 {
    LogType m_type;
    elog::ELogCacheEntryId m_id;
    MsgTest50(LogType type) : m_type(type) {}
    inline void prep() {
        m_id = elog::getOrCacheFormatMsg("Single thread Test log {} 50 chars long xxxxxxxxxx");
    }
    inline void run(elog::ELogLogger* logger, uint32_t msgCount) {
        for (uint32_t i = 0; i < msgCount; ++i) {
            switch (m_type) {
                case LT_NORMAL:
                    ELOG_INFO_EX(logger, "Single thread Test log %u 50 chars long xxxxxxxxxx", i);
                    break;
                case LT_FMT:
                    ELOG_FMT_INFO_EX(logger, "Single thread Test log {} 50 chars long xxxxxxxxxx",
                                     i);
                    break;
                case LT_BIN:
                    ELOG_BIN_INFO_EX(logger, "Single thread Test log {} 50 chars long xxxxxxxxxx",
                                     i);
                    break;
                case LT_BIN_CACHE:
                    ELOG_CACHE_INFO_EX(logger, "Single thread Test log {} 50 chars long xxxxxxxxxx",
                                       i);
                    break;
                case LT_BIN_PRE_CACHE:
                    ELOG_ID_INFO_EX(logger, m_id, i);
                    break;
            }
        }
    }
};

struct MsgTest100 {
    LogType m_type;
    elog::ELogCacheEntryId m_id;
    MsgTest100(LogType type) : m_type(type) {}
    inline void prep() {
        m_id = elog::getOrCacheFormatMsg(
            "Single thread Test log {} 100 chars long "                      // 41
            "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx");  // 59
    }
    inline void run(elog::ELogLogger* logger, uint32_t msgCount) {
        for (uint32_t i = 0; i < msgCount; ++i) {
            switch (m_type) {
                case LT_NORMAL:
                    ELOG_INFO_EX(logger,
                                 "Single thread Test log {} 100 chars long "
                                 "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
                                 i);
                    break;
                case LT_FMT:
                    ELOG_FMT_INFO_EX(logger,
                                     "Single thread Test log {} 100 chars long "
                                     "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
                                     i);
                    break;
                case LT_BIN:
                    ELOG_BIN_INFO_EX(logger,
                                     "Single thread Test log {} 100 chars long "
                                     "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
                                     i);
                    break;
                case LT_BIN_CACHE:
                    ELOG_CACHE_INFO_EX(
                        logger,
                        "Single thread Test log {} 100 chars long "
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
                        i);
                    break;
                case LT_BIN_PRE_CACHE:
                    ELOG_ID_INFO_EX(logger, m_id, i);
                    break;
            }
        }
    }
};

struct MsgTest200 {
    LogType m_type;
    elog::ELogCacheEntryId m_id;
    MsgTest200(LogType type) : m_type(type) {}
    inline void prep() {
        m_id = elog::getOrCacheFormatMsg(
            "Single thread Test log {} 200 chars long "                     // 41
            "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"   // 59
            "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
            "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx");                    // 40
    }
    inline void run(elog::ELogLogger* logger, uint32_t msgCount) {
        for (uint32_t i = 0; i < msgCount; ++i) {
            switch (m_type) {
                case LT_NORMAL:
                    ELOG_INFO_EX(
                        logger,
                        "Single thread Test log {} 200 chars long "                     // 41
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"   // 59
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
                        i);
                    break;
                case LT_FMT:
                    ELOG_FMT_INFO_EX(
                        logger,
                        "Single thread Test log {} 200 chars long "                     // 41
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"   // 59
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
                        i);
                    break;
                case LT_BIN:
                    ELOG_BIN_INFO_EX(
                        logger,
                        "Single thread Test log {} 200 chars long "                     // 41
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"   // 59
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
                        i);
                    break;
                case LT_BIN_CACHE:
                    ELOG_CACHE_INFO_EX(
                        logger,
                        "Single thread Test log {} 200 chars long "                     // 41
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"   // 59
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
                        i);
                    break;
                case LT_BIN_PRE_CACHE:
                    ELOG_ID_INFO_EX(logger, m_id, i);
                    break;
            }
        }
    }
};

struct MsgTest500 {
    LogType m_type;
    elog::ELogCacheEntryId m_id;
    MsgTest500(LogType type) : m_type(type) {}
    inline void prep() {
        m_id = elog::getOrCacheFormatMsg(
            "Single thread Test log {} 500 chars long "                     // 41
            "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"   // 59
            "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
            "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                      // 40
            "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
            "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                      // 40
            "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
            "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                      // 40
            "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
            "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx");                    // 40
    }
    inline void run(elog::ELogLogger* logger, uint32_t msgCount) {
        for (uint32_t i = 0; i < msgCount; ++i) {
            switch (m_type) {
                case LT_NORMAL:
                    ELOG_INFO_EX(
                        logger,
                        "Single thread Test log {} 500 chars long "                     // 41
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"   // 59
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                      // 40
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                      // 40
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                      // 40
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
                        i);
                    break;
                case LT_FMT:
                    ELOG_FMT_INFO_EX(
                        logger,
                        "Single thread Test log {} 500 chars long "                     // 41
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"   // 59
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                      // 40
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                      // 40
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                      // 40
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
                        i);
                    break;
                case LT_BIN:
                    ELOG_BIN_INFO_EX(
                        logger,
                        "Single thread Test log {} 500 chars long "                     // 41
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"   // 59
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                      // 40
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                      // 40
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                      // 40
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
                        i);
                    break;
                case LT_BIN_CACHE:
                    ELOG_CACHE_INFO_EX(
                        logger,
                        "Single thread Test log {} 500 chars long "                     // 41
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"   // 59
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                      // 40
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                      // 40
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                      // 40
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
                        i);
                    break;
                case LT_BIN_PRE_CACHE:
                    ELOG_ID_INFO_EX(logger, m_id, i);
                    break;
            }
        }
    }
};

struct MsgTest1000 {
    LogType m_type;
    elog::ELogCacheEntryId m_id;
    MsgTest1000(LogType type) : m_type(type) {}
    inline void prep() {
        m_id = elog::getOrCacheFormatMsg(
            "Single thread Test log {} 1000 chars long "                    // 42
            "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"    // 58
            "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
            "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                      // 40
            "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
            "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                      // 40
            "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
            "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                      // 40
            "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
            "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                      // 40
            "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
            "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                      // 40
            "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
            "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                      // 40
            "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
            "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                      // 40
            "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
            "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                      // 40
            "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
            "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx");                    // 40
    }
    inline void run(elog::ELogLogger* logger, uint32_t msgCount) {
        for (uint32_t i = 0; i < msgCount; ++i) {
            switch (m_type) {
                case LT_NORMAL:
                    ELOG_INFO_EX(
                        logger,
                        "Single thread Test log {} 1000 chars long "                    // 42
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"    // 58
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                      // 40
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                      // 40
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                      // 40
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                      // 40
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                      // 40
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                      // 40
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                      // 40
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                      // 40
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
                        i);
                    break;
                case LT_FMT:
                    ELOG_FMT_INFO_EX(
                        logger,
                        "Single thread Test log {} 1000 chars long "                    // 42
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"    // 58
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                      // 40
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                      // 40
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                      // 40
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                      // 40
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                      // 40
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                      // 40
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                      // 40
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                      // 40
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
                        i);
                    break;
                case LT_BIN:
                    ELOG_BIN_INFO_EX(
                        logger,
                        "Single thread Test log {} 1000 chars long "                    // 42
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"    // 58
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                      // 40
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                      // 40
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                      // 40
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                      // 40
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                      // 40
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                      // 40
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                      // 40
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                      // 40
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
                        i);
                    break;
                case LT_BIN_CACHE:
                    ELOG_CACHE_INFO_EX(
                        logger,
                        "Single thread Test log {} 1000 chars long "                    // 42
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"    // 58
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                      // 40
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                      // 40
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                      // 40
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                      // 40
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                      // 40
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                      // 40
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                      // 40
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"                      // 40
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 60
                        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
                        i);
                    break;
                case LT_BIN_PRE_CACHE:
                    ELOG_ID_INFO_EX(logger, m_id, i);
                    break;
            }
        }
    }
};

void testPerfMsgLen() {
    // test single-threaded for normal, binary, cached, pre-cached over quantum log target:
    // varying message length, 1 parameter
    const char* cfg =
        "async://quantum?quantum_buffer_size=2000000&name=elog_bench"
        "|file:///./bench_data/"
        "elog_bench_quantum_accel_baseline.log?file_buffer_size=1mb&file_lock=no";
    std::vector<double> normalThroughput;
    std::vector<double> fmtThroughput;
    std::vector<double> binThroughput;
    std::vector<double> binCacheThroughput;
    std::vector<double> binPreCacheThroughput;

    double msgPerf = 0.0f;

#define RUN_MSG_LEN_TEST(len, type, resArray)                          \
    {                                                                  \
        MsgTest##len test##len(type);                                  \
        std::string testName = "Binary Acceleration Message Length ("; \
        testName += std::to_string(len);                               \
        testName += " bytes)";                                         \
        runBinaryAccelTest(testName.c_str(), cfg, test##len, msgPerf); \
        resArray.push_back(msgPerf);                                   \
    }

#define RUN_MSG_LEN_TEST_SET(type, resArray) \
    RUN_MSG_LEN_TEST(50, type, resArray);    \
    RUN_MSG_LEN_TEST(100, type, resArray);   \
    RUN_MSG_LEN_TEST(200, type, resArray);   \
    RUN_MSG_LEN_TEST(500, type, resArray);   \
    RUN_MSG_LEN_TEST(1000, type, resArray);

    RUN_MSG_LEN_TEST_SET(LT_NORMAL, normalThroughput);
    RUN_MSG_LEN_TEST_SET(LT_FMT, fmtThroughput);
    RUN_MSG_LEN_TEST_SET(LT_BIN, binThroughput);
    RUN_MSG_LEN_TEST_SET(LT_BIN_CACHE, binCacheThroughput);
    RUN_MSG_LEN_TEST_SET(LT_BIN_PRE_CACHE, binPreCacheThroughput);

    // write results
    uint32_t tics[] = {10, 100, 200, 500, 1000};
    writeAccelCsvFile("msg_len", "normal", normalThroughput, tics);
    writeAccelCsvFile("msg_len", "fmt", fmtThroughput, tics);
    writeAccelCsvFile("msg_len", "bin", binThroughput, tics);
    writeAccelCsvFile("msg_len", "bin_cache", binCacheThroughput, tics);
    writeAccelCsvFile("msg_len", "bin_pre_cache", binPreCacheThroughput, tics);
}

void testPerfBinaryAcceleration() {
    testPerfParamCount();
    testPerfMsgLen();
}
#endif