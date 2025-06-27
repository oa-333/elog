#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <thread>

#ifdef ELOG_ENABLE_GRPC_CONNECTOR
#include "absl/log/initialize.h"
#include "elog.grpc.pb.h"
#include "elog.pb.h"
#endif
#include "elog_system.h"

#if defined(ELOG_MSVC) || defined(ELOG_MINGW)
#include <x86intrin.h>
inline int64_t elog_rdtscp() {
    unsigned int dummy = 0;
    return __rdtscp(&dummy);
}
#else
#include <x86gprintrin.h>
inline int64_t elog_rdtscp() {
    unsigned int dummy = 0;
    return __rdtscp(&dummy);
}
#endif

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

static const uint64_t MSG_COUNT = 10000;
static const uint32_t MIN_THREAD_COUNT = 1;
static const uint32_t MAX_THREAD_COUNT = 16;
static const char* DEFAULT_CFG = "file:///./bench_data/elog_bench.log";
static bool sTestConns = false;
static int sMsgCnt = -1;
static int sMinThreadCnt = -1;
static int sMaxThreadCnt = -1;

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
static void testGRPC();
static void testGRPCSimple();
static void testGRPCStream();
static void testGRPCAsync();
static void testGRPCAsyncCallbackUnary();
static void testGRPCAsyncCallbackStream();
#endif
#ifdef ELOG_ENABLE_GRAFANA_CONNECTOR
static void testGrafana();
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

static void runSingleThreadedTest(const char* title, const char* cfg, double& msgThroughput,
                                  double& ioThroughput, StatData& msgPercentile,
                                  uint32_t msgCount = MSG_COUNT);
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

void testPerfSTFlushImmediate(std::vector<double>& msgThroughput, std::vector<double>& ioThroughput,
                              std::vector<double>& msgp50, std::vector<double>& msgp95,
                              std::vector<double>& msgp99);
void testPerfSTFlushNever(std::vector<double>& msgThroughput, std::vector<double>& ioThroughput,
                          std::vector<double>& msgp50, std::vector<double>& msgp95,
                          std::vector<double>& msgp99);
void testPerfSTFlushCount4096(std::vector<double>& msgThroughput, std::vector<double>& ioThroughput,
                              std::vector<double>& msgp50, std::vector<double>& msgp95,
                              std::vector<double>& msgp99);
void testPerfSTFlushSize1mb(std::vector<double>& msgThroughput, std::vector<double>& ioThroughput,
                            std::vector<double>& msgp50, std::vector<double>& msgp95,
                            std::vector<double>& msgp99);
void testPerfSTFlushTime200ms(std::vector<double>& msgThroughput, std::vector<double>& ioThroughput,
                              std::vector<double>& msgp50, std::vector<double>& msgp95,
                              std::vector<double>& msgp99);
void testPerfSTBufferedFile1mb(std::vector<double>& msgThroughput,
                               std::vector<double>& ioThroughput, std::vector<double>& msgp50,
                               std::vector<double>& msgp95, std::vector<double>& msgp99);
void testPerfSTDeferredCount4096(std::vector<double>& msgThroughput,
                                 std::vector<double>& ioThroughput, std::vector<double>& msgp50,
                                 std::vector<double>& msgp95, std::vector<double>& msgp99);
void testPerfSTQueuedCount4096(std::vector<double>& msgThroughput,
                               std::vector<double>& ioThroughput, std::vector<double>& msgp50,
                               std::vector<double>& msgp95, std::vector<double>& msgp99);
void testPerfSTQuantumCount4096(std::vector<double>& msgThroughput,
                                std::vector<double>& ioThroughput, std::vector<double>& msgp50,
                                std::vector<double>& msgp95, std::vector<double>& msgp99);

// TODO: check rdtsc for percentile tests
#ifdef ELOG_ENABLE_GRPC_CONNECTOR
#include <grpc/grpc.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>

static std::mutex coutLock;
static void handleLogRecord(const elog_grpc::ELogGRPCRecordMsg* msg) {
    // TODO: conduct a real test - collect messages, verify they match the log messages
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
#endif

// plots:
// file flush count values
// file flush size values
// file flush time values
// flush policies compared
// quantum, deferred Vs. best sync log

static int testConnectors() {
#ifdef ELOG_ENABLE_GRPC_CONNECTOR
    testGRPC();
    testGRPCSimple();
    testGRPCStream();
    testGRPCAsync();
    testGRPCAsyncCallbackUnary();
    testGRPCAsyncCallbackStream();
#endif
#ifdef ELOG_ENABLE_GRAFANA_CONNECTOR
    testGrafana();
#endif
#ifdef ELOG_ENABLE_MYSQL_DB_CONNECTOR
    testMySQL();
#endif
#ifdef ELOG_ENABLE_SQLITE_DB_CONNECTOR
    testSQLite();
#endif
#ifdef ELOG_ENABLE_PGSQL_DB_CONNECTOR
    testPostgreSQL();
#endif
#ifdef ELOG_ENABLE_KAFKA_MSGQ_CONNECTOR
    testKafka();
#endif
#ifdef ELOG_ENABLE_GRAFANA_CONNECTOR
    testGrafana();
#endif
#ifdef ELOG_ENABLE_SENTRY_CONNECTOR
    testSentry();
#endif
#ifdef ELOG_ENABLE_DATADOG_CONNECTOR
    testDatadog();
#endif
    return 0;
}

static bool sTestPerfAll = true;
static bool sTestPerfIdleLog = false;
static bool sTestPerfFileFlush = false;
static bool sTestPerfBufferedFile = false;
static bool sTestPerfDeferredFile = false;
static bool sTestPerfQueuedFile = false;
static bool sTestPerfQuantumPrivateFile = false;
static bool sTestPerfQuantumSharedFile = false;
static bool sTestSingleThread = false;

static bool sTestSingleAll = true;
static bool sTestSingleThreadFlushImmediate = false;
static bool sTestSingleThreadFlushNever = false;
static bool sTestSingleThreadFlushCount = false;
static bool sTestSingleThreadFlushSize = false;
static bool sTestSingleThreadFlushTime = false;
static bool sTestSingleThreadBuffered = false;
static bool sTestSingleThreadDeferred = false;
static bool sTestSingleThreadQueued = false;
static bool sTestSingleThreadQuantum = false;

static bool getPerfParam(const char* param) {
    if (strcmp(param, "idle") == 0) {
        sTestPerfIdleLog = true;
    } else if (strcmp(param, "file") == 0) {
        sTestPerfFileFlush = true;
    } else if (strcmp(param, "buffered") == 0) {
        sTestPerfBufferedFile = true;
    } else if (strcmp(param, "deferred") == 0) {
        sTestPerfDeferredFile = true;
    } else if (strcmp(param, "queued") == 0) {
        sTestPerfQueuedFile = true;
    } else if (strcmp(param, "quantum-private") == 0) {
        sTestPerfQuantumPrivateFile = true;
    } else if (strcmp(param, "quantum-shared") == 0) {
        sTestPerfQuantumSharedFile = true;
    } else if (strcmp(param, "single-thread") == 0) {
        sTestSingleThread = true;
    } else {
        return false;
    }
    sTestPerfAll = false;
    return true;
}

static bool getSingleParam(const char* param) {
    if (strcmp(param, "flush-immediate") == 0) {
        sTestSingleThreadFlushImmediate = true;
    } else if (strcmp(param, "flush-never") == 0) {
        sTestSingleThreadFlushNever = true;
    } else if (strcmp(param, "flush-count") == 0) {
        sTestSingleThreadFlushCount = true;
    } else if (strcmp(param, "flush-size") == 0) {
        sTestSingleThreadFlushSize = true;
    } else if (strcmp(param, "flush-time") == 0) {
        sTestSingleThreadFlushTime = true;
    } else if (strcmp(param, "buffered") == 0) {
        sTestSingleThreadBuffered = true;
    } else if (strcmp(param, "deferred") == 0) {
        sTestSingleThreadDeferred = true;
    } else if (strcmp(param, "queued") == 0) {
        sTestSingleThreadQueued = true;
    } else if (strcmp(param, "quantum") == 0) {
        sTestSingleThreadQuantum = true;
    } else {
        return false;
    }
    sTestSingleAll = false;
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
    if (argc == 2) {
        if (strcmp(argv[1], "--test-conn") == 0) {
            sTestConns = true;
            return true;
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
    // in the future we should also allow specifying count, size, time buffer size, queue params,
    // quantum params, and even entire log target specification
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
        } else {
            fprintf(stderr, "ERROR: Invalid parameter '%s'\n", argv[i]);
            return false;
        }
        ++i;
    }
    return true;
}

int main(int argc, char* argv[]) {
    if (!parseArgs(argc, argv)) {
        return 1;
    }
    if (argc == 2) {
        if (strcmp(argv[1], "--test-conn") == 0) {
            sTestConns = true;
            return testConnectors();
        }
    }
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
    if (sTestPerfAll || sTestSingleThread) {
        testPerfAllSingleThread();
    }
}

void getSamplePercentiles(std::vector<double>& samples, StatData& percentile) {
    std::sort(samples.begin(), samples.end());
    uint32_t sampleCount = samples.size();
    percentile.p50 = samples[sampleCount / 2];
    percentile.p95 = samples[sampleCount * 95 / 100];
    percentile.p99 = samples[sampleCount * 99 / 100];
}

elog::ELogTarget* initElog(const char* cfg /* = DEFAULT_CFG */) {
    if (!elog::ELogSystem::initialize()) {
        fprintf(stderr, "Failed to initialize elog system\n");
        return nullptr;
    }
    fprintf(stderr, "ELog system initialized\n");
    if (sTestConns) {
        elog::ELogSystem::addStdErrLogTarget();
        elog::ELogSystem::setCurrentThreadName("elog_bench_main");
        elog::ELogSystem::setAppName("elog_bench_app");
    }

    elog::ELogPropertyPosSequence props;
    std::string namedCfg = cfg;
    std::string::size_type nonSpacePos = namedCfg.find_first_not_of(" \t\r\n");
    if (nonSpacePos == std::string::npos) {
        fprintf(stderr, "Invalid log target configuration, all white space\n");
        elog::ELogSystem::terminate();
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
            res = elog::ELogSystem::configureFromPropertiesEx(props);
        } else {
            std::string cfgStr = "{ log_target = \"";
            cfgStr += namedCfg + "\"}";
            fprintf(stderr, "Using configuration: log_target = %s\n", namedCfg.c_str());
            res = elog::ELogSystem::configureFromConfigStr(cfgStr.c_str());
        }
    } else {
        res = elog::ELogSystem::configureFromConfigStr(cfg);
    }
    if (!res) {
        fprintf(stderr, "Failed to initialize elog system with log target config: %s\n", cfg);
        elog::ELogSystem::terminate();
        return nullptr;
    }
    fprintf(stderr, "Configure from props OK\n");

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
    fprintf(stderr, "Running Empty Private logger test\n");
    elog::ELogTarget* logTarget = initElog();
    if (logTarget == nullptr) {
        fprintf(stderr, "Failed to init private logger test, aborting\n");
        return;
    }
    fprintf(stderr, "initElog() OK\n");
    elog::ELogLogger* privateLogger = elog::ELogSystem::getPrivateLogger("");
    fprintf(stderr, "private logger retrieved\n");

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
    fprintf(stderr, "Running Empty Shared logger test\n");
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

#ifdef ELOG_ENABLE_GRPC_CONNECTOR
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
    runMultiThreadTest("gRPC (unary)", "elog_bench_grpc_unary", cfg);

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
    runMultiThreadTest("gRPC (stream)", "elog_bench_grpc_stream", cfg);

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
    runMultiThreadTest("gRPC (async)", "elog_bench_grpc_async", cfg);

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
#endif

#ifdef ELOG_ENABLE_MYSQL_DB_CONNECTOR
void testMySQL() {
    const char* cfg =
        "db://mysql?conn_string=tcp://127.0.0.1&db=test&user=root&passwd=root&"
        "insert_query=INSERT INTO log_records VALUES(${rid}, ${time}, ${level}, ${host}, ${user},"
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
        "insert_query=INSERT INTO log_records VALUES(${rid}, ${time}, ${level}, ${host}, ${user},"
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
    const char* cfg =
        "db://postgresql?conn_string=192.168.108.111&port=5432&db=mydb&user=oren&passwd=1234&"
        "insert_query=INSERT INTO log_records VALUES(${rid}, ${time}, ${level}, ${host}, ${user},"
        "${prog}, ${pid}, ${tid}, ${mod}, ${src}, ${msg})&"
        "db_thread_model=conn-per-thread";
    double msgPerf = 0.0f;
    double ioPerf = 0.0f;
    StatData statData;
    runSingleThreadedTest("PostgreSQL", cfg, msgPerf, ioPerf, statData, 10);
}
#endif

#ifdef ELOG_ENABLE_KAFKA_MSGQ_CONNECTOR
void testKafka() {
    const char* cfg =
        "msgq://kafka?kafka_bootstrap_servers=192.168.108.111:9092&"
        "msgq_topic=log_records&"
        "kafka_flush_timeout_millis=50&"
        "flush_policy=immediate&"
        "headers={rid=${rid}, time=${time}, level=${level}, host=${host}, user=${user}, "
        "prog=${prog},"
        "pid = ${pid}, tid = ${tid}, tname = ${tname}, file = ${file}, line = ${line}, func = "
        "${func}"
        "mod = ${mod}, src = ${src}, msg = ${msg}}";
    double msgPerf = 0.0f;
    double ioPerf = 0.0f;
    StatData statData;
    runSingleThreadedTest("Kafka", cfg, msgPerf, ioPerf, statData, 10);
}
#endif

#ifdef ELOG_ENABLE_GRAFANA_CONNECTOR
void testGrafana() {
    const char* cfg =
        "mon://grafana?mode=json&"
        "loki_endpoint=http://192.168.108.111:3100&"
        "labels={\"app\": \"test\"}";
    double msgPerf = 0.0f;
    double ioPerf = 0.0f;
    StatData statData;
    runSingleThreadedTest("Grafana-Loki", cfg, msgPerf, ioPerf, statData, 10);
}
#endif

#ifdef ELOG_ENABLE_SENTRY_CONNECTOR
void testSentry() {
    const char* cfg =
        "mon://sentry?dsn=https://"
        "68a375c6d69b9b1af1ec19d91f98d0c5@o4509530146537472.ingest.de.sentry.io/"
        "4509530351992912&"
        "db_path=.sentry-native&"
        "release=native@1.0&"
        "env=staging&"
        "handler_path=vcpkg_installed\\x64-windows\\tools\\sentry-native\\crashpad_handler.exe&"
        "installed\\x64-windows\\tools\\sentry-native&"
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
    const char* cfg =
        "mon://datadog?endpoint=https://http-intake.logs.datadoghq.eu&"
        "api_key=670d32934fa0d393561050a42c6ef7db&"
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
    runSingleThreadedTest("Datadog", cfg, msgPerf, ioPerf, statData, 10);
}
#endif

void runSingleThreadedTest(const char* title, const char* cfg, double& msgThroughput,
                           double& ioThroughput, StatData& msgPercentile,
                           uint32_t msgCount /* = MSG_COUNT */) {
    if (sMsgCnt > 0) {
        msgCount = sMsgCnt;
    }
    elog::ELogTarget* logTarget = initElog(cfg);
    if (logTarget == nullptr) {
        fprintf(stderr, "Failed to init %s test, aborting\n", title);
        return;
    }

    fprintf(stderr, "\n\nRunning %s single-thread test\n", title);
    elog::ELogSource* logSource = elog::ELogSystem::defineLogSource("elog.bench", true);
    elog::ELogLogger* logger = logSource->createPrivateLogger();
#ifdef MEASURE_PERCENTILE
    std::vector<double> samples(msgCount, 0.0f);
#endif
    ELOG_ERROR_EX(logger, "This is a test error message");
    uint64_t bytesStart = logTarget->getBytesWritten();
    auto start = std::chrono::high_resolution_clock::now();
    for (uint64_t i = 0; i < msgCount; ++i) {
#ifdef MEASURE_PERCENTILE
        auto logStart = std::chrono::high_resolution_clock::now();
#endif
        ELOG_INFO_EX(logger, "Single thread Test log %u", i);
        auto logEnd = std::chrono::high_resolution_clock::now();
#ifdef MEASURE_PERCENTILE
        samples[i] =
            std::chrono::duration_cast<std::chrono::microseconds>(logEnd - logStart).count();
#endif
    }
    auto end0 = std::chrono::high_resolution_clock::now();
    fprintf(stderr, "Finished logging, waiting for logger to catch up\n");
    uint64_t writeCount = 0;
    uint64_t readCount = 0;
    // uint64_t waitCount = 0;
    while (!logTarget->isCaughtUp(writeCount, readCount)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(0));
        /*if (++waitCount % 10000 == 0) {
            fprintf(stderr, "%" PRIu64 "\\%" PRIu64 "\r", readCount, writeCount);
        }*/
    }
    // fputs("\n", stderr);
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
    fprintf(stderr, "Throughput: %0.3f KB/Sec\n", ioThroughput);
#ifdef MEASURE_PERCENTILE
    getSamplePercentiles(samples, msgPercentile);
#endif

    termELog();
}

void runMultiThreadTest(const char* title, const char* fileName, const char* cfg,
                        bool privateLogger /* = true */,
                        uint32_t maxThreads /* = MAX_THREAD_COUNT */) {
    uint32_t minThreads = sMinThreadCnt >= 0 ? sMinThreadCnt : MIN_THREAD_COUNT;
    if (sMaxThreadCnt > 0) {
        maxThreads = sMaxThreadCnt;
    }
    uint32_t msgCount = MSG_COUNT;
    if (sMsgCnt > 0) {
        msgCount = sMsgCnt;
    }
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
    for (uint32_t threadCount = minThreads; threadCount <= maxThreads; ++threadCount) {
        // fprintf(stderr, "Running %u threads test\n", threadCount);
        ELOG_INFO("Running %u Thread Test", threadCount);
        std::vector<std::thread> threads;
        std::vector<double> resVec(threadCount, 0.0);
        auto start = std::chrono::high_resolution_clock::now();
        uint64_t bytesStart = logTarget->getBytesWritten();
        for (uint32_t i = 0; i < threadCount; ++i) {
            threads.emplace_back(std::thread([i, &resVec, sharedLogger, msgCount]() {
                elog::ELogLogger* logger =
                    sharedLogger != nullptr ? sharedLogger : elog::ELogSystem::getPrivateLogger("");
                auto start = std::chrono::high_resolution_clock::now();
                for (uint64_t j = 0; j < msgCount; ++j) {
                    ELOG_INFO_EX(logger, "Thread %u Test log %u", i, j);
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
        uint64_t writeCount = 0;
        uint64_t readCount = 0;
        // uint64_t waitCount = 0;
        while (!logTarget->isCaughtUp(writeCount, readCount)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(0));
            /*if (++waitCount % 10000 == 0) {
                fprintf(stderr, "%" PRIu64 "\\%" PRIu64 "\r", readCount, writeCount);
            }*/
            /*fprintf(stderr, "write-pos = %" PRIu64 ", read-pos = %" PRIu64 "\n", writeCount,
                    readCount);*/
        }
        // fputs("\n", stderr);
        //  fprintf(stderr, "write-pos = %" PRIu64 ", read-pos = %" PRIu64 "\n", writeCount,
        //  readCount);
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
        throughput = threadCount * msgCount / (double)testTime0.count() * 1000000.0f;
        /*fprintf(stderr, "%u thread Test time: %u usec, msg count: %u\n", threadCount,
                (unsigned)testTime.count(), (unsigned)MSG_COUNT);*/
        fprintf(stderr, "%u thread Throughput: %0.3f MSg/Sec\n", threadCount, throughput);
        msgThroughput.push_back(throughput);
        throughput = (bytesEnd - bytesStart) / (double)testTime.count() * 1000000.0f / 1024;
        fprintf(stderr, "%u thread Throughput: %0.3f KB/Sec\n", threadCount, throughput);
        byteThroughput.push_back(throughput);
    }

    // std::this_thread::sleep_for(std::chrono::milliseconds(5000));
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
        "file:///./bench_data/"
        "elog_bench_buffered512.log?file_buffer_size=512&file_lock=yes&flush_policy=none";
    runMultiThreadTest("Buffered File (512 bytes)", "elog_bench_buffered512", cfg);

    cfg =
        "file:///./bench_data/"
        "elog_bench_buffered4kb.log?file_buffer_size=4096&file_lock=yes&flush_policy=none";
    runMultiThreadTest("Buffered File (4kb)", "elog_bench_buffered4kb", cfg);

    cfg =
        "file:///./bench_data/"
        "elog_bench_buffered64kb.log?file_buffer_size=65536&file_lock=yes&flush_policy=none";
    runMultiThreadTest("Buffered File (64kb)", "elog_bench_buffered64kb", cfg);

    cfg =
        "file:///./bench_data/"
        "elog_bench_buffered1mb.log?file_buffer_size=1048576&file_lock=yes&flush_policy=none";
    runMultiThreadTest("Buffered File (1mb)", "elog_bench_buffered1mb", cfg);

    cfg =
        "file:///./bench_data/"
        "elog_bench_buffered4mb.log?file_buffer_size=4194304&file_lock=yes&flush_policy=none";
    runMultiThreadTest("Buffered File (4mb)", "elog_bench_buffered4mb", cfg);
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
        "async://deferred?flush_policy=count&flush_count=4096&name=elog_bench#"
        "file:///./bench_data/elog_bench_deferred.log?file_buffer_size=4096&file_lock=no";
    runMultiThreadTest("Deferred (Flush Count 4096)", "elog_bench_deferred", cfg);
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
        "async://queued?queue_batch_size=10000&queue_timeout_millis=200&"
        "flush_policy=count&flush_count=4096&name=elog_bench#"
        "file:///./bench_data/elog_bench_queued.log?file_buffer_size=4096&file_lock=no";
    runMultiThreadTest("Queued 4096 + 200ms (Flush Count 4096)", "elog_bench_queued", cfg);
}

void testPerfQuantumFile(bool privateLogger) {
    /*const char* cfg =
        "file://./bench_data/"
        "elog_bench_quantum.log?file_buffer_size=4194304&file_lock=no&flush_policy=count&flush-"
        "count=4096&quantum_buffer_size=2000000";*/
    const char* cfg =
        "async://"
        "quantum?quantum_buffer_size=2000000&flush_policy=count&flush_count=4096&name=elog_bench"
        "#file:///./bench_data/elog_bench_quantum.log?file_buffer_size=4096&file_lock=no";
    runMultiThreadTest("Quantum 200000 (Flush Count 4096)", "elog_bench_quantum", cfg,
                       privateLogger);
}

static void writeSTCsv(const char* fname, const std::vector<double>& data) {
    std::ofstream f(fname, std::ios_base::trunc);
    int column = 0;
    f << column << " \"Flush\\nImmediate\" " << std::fixed << std::setprecision(2) << data[column++]
      << std::endl;
    f << column << " \"Flush\\nNever\" " << std::fixed << std::setprecision(2) << data[column++]
      << std::endl;
    f << column << " \"Flush\\nCount=4096\" " << std::fixed << std::setprecision(2)
      << data[column++] << std::endl;
    f << column << " \"Flush\\nSize=1MB\" " << std::fixed << std::setprecision(2) << data[column++]
      << std::endl;
    f << column << " \"Flush\\nTime=200ms\" " << std::fixed << std::setprecision(2)
      << data[column++] << std::endl;
    f << column << " \"Buffered\\nSize=1MB\" " << std::fixed << std::setprecision(2)
      << data[column++] << std::endl;
    f << column << " Deferred " << std::fixed << std::setprecision(2) << data[column++]
      << std::endl;
    f << column << " Queued " << std::fixed << std::setprecision(2) << data[column++] << std::endl;
    f << column << " Quantum " << std::fixed << std::setprecision(2) << data[column++] << std::endl;
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
    if (sTestSingleAll || sTestSingleThreadDeferred) {
        testPerfSTDeferredCount4096(msgThroughput, ioThroughput, msgp50, msgp95, msgp99);
    }
    if (sTestSingleAll || sTestSingleThreadQueued) {
        testPerfSTQueuedCount4096(msgThroughput, ioThroughput, msgp50, msgp95, msgp99);
    }
    if (sTestSingleAll || sTestSingleThreadQuantum) {
        testPerfSTQuantumCount4096(msgThroughput, ioThroughput, msgp50, msgp95, msgp99);
    }

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
        "elog_bench_flush_size_1mb_st.log?flush_policy=size&flush_size_bytes=1048576";
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
        "elog_bench_flush_time_200ms_st.log?flush_policy=time&flush_timeout_millis=200";
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
        "elog_bench_buffered_1mb_st.log?file_buffer_size=1048576&flush_policy=none";
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

void testPerfSTDeferredCount4096(std::vector<double>& msgThroughput,
                                 std::vector<double>& ioThroughput, std::vector<double>& msgp50,
                                 std::vector<double>& msgp95, std::vector<double>& msgp99) {
    /*const char* cfg =
        "file://./bench_data/"
        "elog_bench_deferred_st.log?file_buffer_size=4194304&file_lock=no&flush_policy=count&flush-"
        "count=4096&deferred";*/
    const char* cfg =
        "async://deferred?flush_policy=count&flush_count=4096&name=elog_bench#"
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
        "async://queued?queue_batch_size=10000&queue_timeout_millis=500&"
        "flush_policy=count&flush_count=4096&name=elog_bench#"
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
        "           file_buffer_size = 4096,"
        "           file_lock = no"
        "       }"
        "   }"
        "}";
    /*const char* cfg =
        "file://./bench_data/elog_bench_quantum.log?file_buffer_size=4096&file_lock=no&"
        //"flush_policy=count&flush_count=4096&"
        "quantum_buffer_size=2000000";*/
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

void testPerfFileNeverFlushPolicy() {
    const char* cfg = "file:///./bench_data/elog_bench_flush_never.log?flush_policy=never";
    runMultiThreadTest("File (Never Flush Policy)", "elog_bench_flush_never", cfg);
}

static void testPerfImmediateFlushPolicy() {
    const char* cfg = "file:///./bench_data/elog_bench_flush_immediate.log?flush_policy=immediate";
    runMultiThreadTest("File (Immediate Flush Policy)", "elog_bench_flush_immediate", cfg);
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
        "file:///./bench_data/elog_bench_size64.log?flush_policy=size&flush_size_bytes=64";
    runMultiThreadTest("File (Size 64 bytes Flush Policy)", "elog_bench_size64", cfg);

    cfg = "file:///./bench_data/elog_bench_size_1kb.log?flush_policy=size&flush_size_bytes=1024";
    runMultiThreadTest("File (Size 1KB Flush Policy)", "elog_bench_size_1kb", cfg);

    cfg = "file:///./bench_data/elog_bench_size_4kb.log?flush_policy=size&flush_size_bytes=4096";
    runMultiThreadTest("File (Size 4KB Flush Policy)", "elog_bench_size_4kb", cfg);

    cfg = "file:///./bench_data/elog_bench_size_64kb.log?flush_policy=size&flush_size_bytes=65536";
    runMultiThreadTest("File (Size 64KB Flush Policy)", "elog_bench_size_64kb", cfg);

    cfg =
        "file:///./bench_data/"
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
        "file:///./bench_data/"
        "elog_bench_time_200ms.log?flush_policy=time&flush_timeout_millis=200";
    runMultiThreadTest("File (Time 200 ms Flush Policy)", "elog_bench_time_200ms", cfg);

    cfg =
        "file:///./bench_data/"
        "elog_bench_time_500ms.log?flush_policy=time&flush_timeout_millis=500";
    runMultiThreadTest("File (Time 500 ms Flush Policy)", "elog_bench_time_500ms", cfg);

    cfg =
        "file:///./bench_data/"
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