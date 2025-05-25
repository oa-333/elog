#ifndef __ELOG_GRPC_TARGET_H__
#define __ELOG_GRPC_TARGET_H__

#include "elog_def.h"

#define ELOG_ENABLE_GRPC_CONNECTOR
#ifdef ELOG_ENABLE_GRPC_CONNECTOR

#include <grpc/grpc.h>
#include <grpcpp/channel.h>

#include "elog.grpc.pb.h"
#include "elog.pb.h"
#include "elog_rpc_target.h"

#define ELOG_GRPC_DEFAULT_MAX_INFLIGHT_CALLS 1024

// gRPC log target has the following possible configurations:
// - simple: each log record is sent synchronously, flush does nothing
// - streaming: each log record is sent through a writer, only during flush the writer calls
// WritesDone(), so never flush policy is not allowed, and immediate flush policy will have adverse
// effect, as it acts like simple mode but with more overhead
// - async: in this mode a completion queue is used, as in a synchronous, where a send-receive

namespace elog {

enum class ELogGRPCClientMode : uint32_t {
    /** @var Unary client. */
    GRPC_CM_UNARY,

    /** @var Steaming client. */
    GRPC_CM_STREAM,

    /** @var Asynchronous client with completion queue. */
    GRPC_CM_ASYNC,

    /** @var Asynchronous client with callback, employing unary reactor. */
    GRPC_CM_ASYNC_CALLBACK_UNARY,

    /** @var Asynchronous client with callback, employing stream reactor. */
    GRPC_CM_ASYNC_CALLBACK_STREAM
};

class ELogGRPCFieldReceptor : public ELogFieldReceptor {
public:
    ELogGRPCFieldReceptor() : m_logRecordMsg(nullptr) {}
    ~ELogGRPCFieldReceptor() final {}

    /** @brief Provide from outside a log record message to be filled-in by the field receptor. */
    inline void setLogRecordMsg(elog_grpc::ELogGRPCRecordMsg* logRecordMsg) {
        m_logRecordMsg = logRecordMsg;
    }

    /** @brief Receives a string log record field. */
    void receiveStringField(uint32_t typeId, const std::string& value, int justify) final;

    /** @brief Receives an integer log record field. */
    void receiveIntField(uint32_t typeId, uint64_t value, int justify) final;

#ifdef ELOG_MSVC
    /** @brief Receives a time log record field. */
    void receiveTimeField(uint32_t typeId, const SYSTEMTIME& sysTime, const char* timeStr,
                          int justify) final;
#else
    /** @brief Receives a time log record field. */
    void receiveTimeField(uint32_t typeId, const timeval& sysTime, const char* timeStr,
                          int justify) final;
#endif

    /** @brief Receives a log level log record field. */
    void receiveLogLevelField(uint32_t typeId, ELogLevel logLevel, int justify) final;

private:
    elog_grpc::ELogGRPCRecordMsg* m_logRecordMsg;
};

// the client write reactor used with asynchronous callback streaming
// unfortunately, in order to make this code (mostly) lock-free, the implementation had to be a bit
// complex
class ELogReactor final : public grpc::ClientWriteReactor<elog_grpc::ELogGRPCRecordMsg> {
public:
    ELogReactor(elog_grpc::ELogGRPCService::Stub* stub, ELogRpcFormatter* rpcFormatter,
                uint32_t maxInflightCalls = ELOG_GRPC_DEFAULT_MAX_INFLIGHT_CALLS)
        : m_stub(stub),
          m_rpcFormatter(rpcFormatter),
          m_state(ReactorState::RS_INIT),
          m_inFlight(false),
          m_inFlightRequestId(0),
          m_nextRequestId(0) {
        // just to be on the safe side
        if (maxInflightCalls == 0) {
            maxInflightCalls = ELOG_GRPC_DEFAULT_MAX_INFLIGHT_CALLS;
        }
        m_inFlightRequests.resize(maxInflightCalls);
    }
    ~ELogReactor() {}

    /**
     * @brief Writes a log record through the reactor (outside reactor flow).
     * @param logRecord The log record to pass to the reactor.
     * @return uint32_t The number of bytes written.
     */
    uint32_t writeLogRecord(const ELogRecord& logRecord);

    /**
     * @brief Submits a flush request to the log reactor. In effects marks the end of a single RPC
     * stream. This call returns immediately, and does not wait for flush to actually be executed.
     */
    void flush();

    /** @brief Waits for the last submitted flush request to be fully executed.*/
    bool waitFlushDone();

    /**
     * @brief React to gRPC event: single log message has been written, and a new one can be
     * submitted.
     * @param ok The previous message's write result.
     */
    void OnWriteDone(bool ok) override;

    /**
     * @brief React to gRPC event: a stream RPC has ended.
     * @param status The result status for the entire RPC call.
     */
    void OnDone(const grpc::Status& status) override;

private:
    grpc::ClientContext m_context;
    grpc::Status m_status;
    elog_grpc::ELogGRPCService::Stub* m_stub;
    ELogRpcFormatter* m_rpcFormatter;
    std::mutex m_lock;
    std::condition_variable m_cv;
    enum class ReactorState : uint32_t { RS_INIT, RS_BATCH, RS_FLUSH, RS_DONE };
    std::atomic<ReactorState> m_state;
    std::atomic<bool> m_inFlight;
    std::atomic<uint64_t> m_inFlightRequestId;

    template <typename T>
    struct atomic_wrapper {
        std::atomic<T> m_atomicValue;
        atomic_wrapper() : m_atomicValue() {}
        atomic_wrapper(const T& value) : m_atomicValue(value) {}
        atomic_wrapper(const std::atomic<T>& atomicValue)
            : m_atomicValue(atomicValue.load(std::memory_order_relaxed)) {}
        atomic_wrapper(const atomic_wrapper& other)
            : m_atomicValue(other.m_atomicValue.load(std::memory_order_relaxed)) {}
        atomic_wrapper& operator=(const atomic_wrapper& other) {
            m_atomicValue.store(other.m_atomicValue.load(std::memory_order_relaxed),
                                std::memory_order_relaxed);
            return *this;
        }
    };

    struct CallData {
        atomic_wrapper<uint64_t> m_requestId;
        atomic_wrapper<bool> m_isUsed;
        elog_grpc::ELogGRPCRecordMsg* m_logRecordMsg;
        ELogGRPCFieldReceptor m_receptor;

        CallData() : m_requestId(0), m_isUsed(false), m_logRecordMsg(nullptr) {}

        void init(uint64_t requestId) {
            m_requestId.m_atomicValue.store(requestId, std::memory_order_relaxed);
            m_logRecordMsg = new (std::nothrow) elog_grpc::ELogGRPCRecordMsg();
            m_receptor.setLogRecordMsg(m_logRecordMsg);
        }

        void clear() {
            m_requestId.m_atomicValue.store(-1, std::memory_order_relaxed);
            m_isUsed.m_atomicValue.store(false, std::memory_order_relaxed);
            // NOTE: the grpc framework does not take ownership of the message so we must delete it
            delete m_logRecordMsg;
            m_logRecordMsg = nullptr;
            m_receptor.setLogRecordMsg(nullptr);
        }
    };

    std::vector<CallData> m_inFlightRequests;
    std::atomic<uint64_t> m_nextRequestId;

    std::list<uint64_t> m_pendingWriteRequests;

    CallData* allocCallData();

    bool setStateFlush();

    void setStateInit();
};

class ELOG_API ELogGRPCTarget : public ELogRpcTarget {
public:
    ELogGRPCTarget(const std::string& server, const std::string& params,
                   ELogGRPCClientMode clientMode = ELogGRPCClientMode::GRPC_CM_UNARY,
                   uint32_t deadlineTimeoutMillis = 0, uint32_t maxInflightCalls = 0)
        : ELogRpcTarget(server.c_str(), "", 0, ""),
          m_params(params),
          m_clientMode(clientMode),
          m_deadlineTimeoutMillis(deadlineTimeoutMillis),
          m_maxInflightCalls(maxInflightCalls),
          m_streamContext(nullptr),
          m_reactor(nullptr) {}

    ELogGRPCTarget(const ELogGRPCTarget&) = delete;
    ELogGRPCTarget(ELogGRPCTarget&&) = delete;
    ~ELogGRPCTarget() final {}

protected:
    /** @brief Order the log target to start (required for threaded targets). */
    bool startLogTarget() final;

    /** @brief Order the log target to stop (required for threaded targets). */
    bool stopLogTarget() final;

    /** @brief Sends a log record to a log target. */
    uint32_t writeLogRecord(const ELogRecord& logRecord) final;

    /** @brief Orders a buffered log target to flush it log messages. */
    void flushLogTarget() final;

private:
    // configuration
    std::string m_params;
    ELogGRPCClientMode m_clientMode;
    uint32_t m_deadlineTimeoutMillis;
    uint32_t m_maxInflightCalls;

    // the stub
    std::unique_ptr<elog_grpc::ELogGRPCService::Stub> m_serviceStub;

    // synchronous stream mode members
    grpc::ClientContext* m_streamContext;
    elog_grpc::ELogGRPCStatus m_streamStatus;
    std::unique_ptr<grpc::ClientWriter<elog_grpc::ELogGRPCRecordMsg>> m_clientWriter;

    // asynchronous unary mode members
    grpc::CompletionQueue m_cq;

    // the reactor used for asynchronous callback streaming mode
    ELogReactor* m_reactor;

    /** @brief Sends a log record to a log target. */
    uint32_t writeLogRecordUnary(const ELogRecord& logRecord);
    uint32_t writeLogRecordStream(const ELogRecord& logRecord);
    uint32_t writeLogRecordAsync(const ELogRecord& logRecord);
    uint32_t writeLogRecordAsyncCallbackUnary(const ELogRecord& logRecord);
    uint32_t writeLogRecordAsyncCallbackStream(const ELogRecord& logRecord);

    // helper method to set single RPC call deadline
    inline void setDeadline(grpc::ClientContext& context) {
        std::chrono::time_point deadlineMillis =
            std::chrono::system_clock::now() + std::chrono::milliseconds(m_deadlineTimeoutMillis);
        context.set_deadline(deadlineMillis);
    }

    bool createStreamContext();
    void destroyStreamContext();

    bool createStreamWriter();
    bool flushStreamWriter();
    void destroyStreamWriter();

    bool createReactor();
    bool flushReactor();
    void destroyReactor();
};

// TODO: asynchronous callback stream is by far the fastest (22K msg.sec vs at most 7k msg/sec)
// but it needs more fixes:
// [DONE] 1. use static CallData and avoid allocating on heap, just allocate once and use vacant
// flag
// [DONE] 2. cleanup code, consider using strategy pattern to avoid confusing implementations (there
// are 5)
// [ABANDONED] 3. it is possible that async queue can work faster, but docs recommend using callback
// API
// [DONE] 4. async callback streaming client needs to configure max_inflight_calls, such that
// beyond that the logging application will block
// [DONE] 5. looks like deferred target with gRPC target gets stuck (spin on CPU)

}  // namespace elog

#endif  // ELOG_ENABLE_KAFKA_MSGQ_CONNECTOR

#endif  // __ELOG_KAFKA_MSGQ_TARGET_H__