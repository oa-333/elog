#ifndef __ELOG_GRPC_TARGET_H__
#define __ELOG_GRPC_TARGET_H__

#include "elog_def.h"

#ifdef ELOG_ENABLE_GRPC_CONNECTOR

#include <grpc/grpc.h>
#include <grpcpp/channel.h>

// disable some annoying warnings coming from protobuf generated headers
#ifdef ELOG_MSVC
#pragma warning(push)
#pragma warning(disable : 4626 5027)
#endif

#include "elog_proto.h"

#ifdef ELOG_MSVC
#pragma warning(pop)
#endif

#include "elog_atomic.h"
#include "elog_report_handler.h"
#include "elog_rpc_target.h"

/** @def Default deadline used by gRPC log target. Beware of too small deadlines. */
#define ELOG_GRPC_DEFAULT_DEADLINE_MILLIS 60000

/** @def Default maximum number of pending messages used by the reactor code. */
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

template <typename MessageType = elog_grpc::ELogRecordMsg>
class ELogGRPCBaseReceptor : public ELogFieldReceptor {
public:
    ELogGRPCBaseReceptor()
        : ELogFieldReceptor(ELogFieldReceptor::ReceiveStyle::RS_BY_NAME), m_logRecordMsg(nullptr) {}
    ELogGRPCBaseReceptor(const ELogGRPCBaseReceptor&) = delete;
    ELogGRPCBaseReceptor(ELogGRPCBaseReceptor&&) = delete;
    ELogGRPCBaseReceptor& operator=(const ELogGRPCBaseReceptor&) = delete;
    ~ELogGRPCBaseReceptor() override {}

    /** @brief Provide from outside a log record message to be filled-in by the field receptor. */
    inline void setLogRecordMsg(MessageType* logRecordMsg) { m_logRecordMsg = logRecordMsg; }

    /** @brief Receives any static text found outside of log record field references. */
    void receiveStaticText(uint32_t typeId, const std::string& text,
                           const ELogFieldSpec& fieldSpec) override;

    /** @brief Receives the log record id. */
    void receiveRecordId(uint32_t typeId, uint64_t recordId,
                         const ELogFieldSpec& fieldSpec) override;

    /** @brief Receives the host name. */
    void receiveHostName(uint32_t typeId, const char* hostName,
                         const ELogFieldSpec& fieldSpec) override;

    /** @brief Receives the user name. */
    void receiveUserName(uint32_t typeId, const char* userName,
                         const ELogFieldSpec& fieldSpec) override;

    /** @brief Receives the program name. */
    void receiveProgramName(uint32_t typeId, const char* programName,
                            const ELogFieldSpec& fieldSpec) override;

    /** @brief Receives the process id. */
    void receiveProcessId(uint32_t typeId, uint64_t processId,
                          const ELogFieldSpec& fieldSpec) override;

    /** @brief Receives the thread id. */
    void receiveThreadId(uint32_t typeId, uint64_t threadId,
                         const ELogFieldSpec& fieldSpec) override;

    /** @brief Receives the thread name. */
    void receiveThreadName(uint32_t typeId, const char* threadName,
                           const ELogFieldSpec& fieldSpec) override;

    /** @brief Receives the log source name. */
    void receiveLogSourceName(uint32_t typeId, const char* logSourceName,
                              const ELogFieldSpec& fieldSpec) override;

    /** @brief Receives the module name. */
    void receiveModuleName(uint32_t typeId, const char* moduleName,
                           const ELogFieldSpec& fieldSpec) override;

    /** @brief Receives the file name. */
    void receiveFileName(uint32_t typeId, const char* fileName,
                         const ELogFieldSpec& fieldSpec) override;

    /** @brief Receives the logging line. */
    void receiveLineNumber(uint32_t typeId, uint64_t lineNumber,
                           const ELogFieldSpec& fieldSpec) override;

    /** @brief Receives the function name. */
    void receiveFunctionName(uint32_t typeId, const char* functionName,
                             const ELogFieldSpec& fieldSpec) override;

    /** @brief Receives the log msg. */
    void receiveLogMsg(uint32_t typeId, const char* logMsg,
                       const ELogFieldSpec& fieldSpec) override;

    /** @brief Receives a string log record field. */
    void receiveStringField(uint32_t typeId, const char* value, const ELogFieldSpec& fieldSpec,
                            size_t length) override;

    /** @brief Receives an integer log record field. */
    void receiveIntField(uint32_t typeId, uint64_t value, const ELogFieldSpec& fieldSpec) override;

    /** @brief Receives a time log record field. */
    void receiveTimeField(uint32_t typeId, const ELogTime& logTime, const char* timeStr,
                          const ELogFieldSpec& fieldSpec, size_t length) override;

    /** @brief Receives a log level log record field. */
    void receiveLogLevelField(uint32_t typeId, ELogLevel logLevel,
                              const ELogFieldSpec& fieldSpec) override;

private:
    MessageType* m_logRecordMsg;
};

typedef ELogGRPCBaseReceptor<> ELogGRPCReceptor;

// the client write reactor used with asynchronous callback streaming
// unfortunately, in order to make this code (mostly) lock-free, the implementation had to be a bit
// complex
template <typename StubType = elog_grpc::ELogService::Stub,
          typename MessageType = elog_grpc::ELogRecordMsg, typename ReceptorType = ELogGRPCReceptor>
class ELogGRPCBaseReactor : public grpc::ClientWriteReactor<MessageType> {
public:
    ELogGRPCBaseReactor(ELogReportHandler* reportHandler, StubType* stub,
                        ELogRpcFormatter* rpcFormatter,
                        uint32_t maxInflightCalls = ELOG_GRPC_DEFAULT_MAX_INFLIGHT_CALLS)
        : m_logger("grpc.ELogGRPCBaseReactor"),
          m_reportHandler(reportHandler),
          m_stub(stub),
          m_rpcFormatter(rpcFormatter),
          m_state(ReactorState::RS_INIT),
          m_inFlight(false),
          m_inFlightRequestId(0),
          m_inFlightRequests(nullptr),
          m_maxInflightCalls(maxInflightCalls),
          m_nextRequestId(0) {
        // just to be on the safe side
        if (m_maxInflightCalls == 0) {
            m_maxInflightCalls = ELOG_GRPC_DEFAULT_MAX_INFLIGHT_CALLS;
        }
        m_inFlightRequests = new (std::nothrow) CallData[m_maxInflightCalls];
        if (m_inFlightRequests == nullptr) {
            m_reportHandler->onReport(
                m_logger, ELEVEL_ERROR, __FILE__, __LINE__, ELOG_FUNCTION,
                "Failed to allocate in-flight message array in gRPC base reactor");
        }
    }
    ELogGRPCBaseReactor(const ELogGRPCBaseReactor&) = delete;
    ELogGRPCBaseReactor(ELogGRPCBaseReactor&&) = delete;
    ELogGRPCBaseReactor& operator=(const ELogGRPCBaseReactor&) = delete;
    ~ELogGRPCBaseReactor() override {
        if (m_inFlightRequests != nullptr) {
            delete[] m_inFlightRequests;
            m_inFlightRequests = nullptr;
        }
    }

    /**
     * @brief Writes a log record through the reactor (outside reactor flow).
     * @param logRecord The log record to pass to the reactor.
     * @param[out] bytesWritten The number of bytes written.
     * @return The operation's result.
     */
    bool writeLogRecord(const ELogRecord& logRecord, uint64_t& bytesWritten);

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
    ELogReportLogger m_logger;
    ELogReportHandler* m_reportHandler;
    grpc::ClientContext m_context;
    grpc::Status m_status;
    StubType* m_stub;
    ELogRpcFormatter* m_rpcFormatter;
    std::mutex m_lock;
    std::condition_variable m_cv;
    enum class ReactorState : uint32_t { RS_INIT, RS_BATCH, RS_FLUSH, RS_DONE };
    std::atomic<ReactorState> m_state;
    std::atomic<bool> m_inFlight;
    std::atomic<uint64_t> m_inFlightRequestId;

    typedef grpc::ClientWriteReactor<MessageType> BaseClass;

    struct CallData {
        ELogAtomic<uint64_t> m_requestId;
        ELogAtomic<bool> m_isUsed;
        MessageType* m_logRecordMsg;
        ReceptorType m_receptor;

        CallData();
        CallData(const CallData&) = delete;
        CallData(CallData&&) = delete;
        CallData& operator=(const CallData&) = delete;
        ~CallData() {}

        bool init(uint64_t requestId) {
            m_requestId.m_atomicValue.store(requestId, std::memory_order_relaxed);
            m_logRecordMsg = new (std::nothrow) MessageType();
            if (m_logRecordMsg == nullptr) {
                return false;
            }
            m_receptor.setLogRecordMsg(m_logRecordMsg);
            return true;
        }

        void clear();
    };

    CallData* m_inFlightRequests;
    uint32_t m_maxInflightCalls;
    std::atomic<uint64_t> m_nextRequestId;

    std::list<uint64_t> m_pendingWriteRequests;

    CallData* allocCallData();

    bool setStateFlush();
};

typedef ELogGRPCBaseReactor<> ELogGRPCReactor;

template <
    typename ServiceType = elog_grpc::ELogService, typename StubType = typename ServiceType::Stub,
    typename MessageType = elog_grpc::ELogRecordMsg,
    typename ResponseType = elog_grpc::ELogStatusMsg, typename ReceptorType = ELogGRPCReceptor>
class ELogGRPCBaseTarget : public ELogRpcTarget {
public:
    ELogGRPCBaseTarget(ELogReportHandler* reportHandler, const std::string& server,
                       const std::string& params, const std::string& serverCA,
                       const std::string& clientCA, const std::string& clientKey,
                       ELogGRPCClientMode clientMode = ELogGRPCClientMode::GRPC_CM_UNARY,
                       uint64_t deadlineTimeoutMillis = 0, uint32_t maxInflightCalls = 0)
        : ELogRpcTarget(server.c_str(), "", 0, ""),
          m_logger("grpc.ELogGRPCBaseTarget"),
          m_reportHandler(reportHandler),
          m_params(params),
          m_serverCA(serverCA),
          m_clientCA(clientCA),
          m_clientKey(clientKey),
          m_clientMode(clientMode),
          m_maxInflightCalls(maxInflightCalls),
          m_deadlineTimeoutMillis(deadlineTimeoutMillis),
          m_streamContext(nullptr),
          m_reactor(nullptr) {}

    ELogGRPCBaseTarget(const ELogGRPCBaseTarget&) = delete;
    ELogGRPCBaseTarget(ELogGRPCBaseTarget&&) = delete;
    ELogGRPCBaseTarget& operator=(const ELogGRPCBaseTarget&) = delete;

    void destroy() final { delete this; }

protected:
    /** @brief Order the log target to start (required for threaded targets). */
    bool startLogTarget() final;

    /** @brief Order the log target to stop (required for threaded targets). */
    bool stopLogTarget() final;

    /**
     * @brief Order the log target to write a log record (thread-safe).
     * @param logRecord The log record to write to the log target.
     * @param bytesWritten The number of bytes written to log.
     * @return The operation's result.
     */
    bool writeLogRecord(const ELogRecord& logRecord, uint64_t& bytesWritten) final;

    /** @brief Orders a buffered log target to flush it log messages. */
    bool flushLogTarget() final;

private:
    // configuration
    ELogReportLogger m_logger;
    ELogReportHandler* m_reportHandler;
    std::string m_params;
    std::string m_serverCA;
    std::string m_clientCA;
    std::string m_clientKey;
    ELogGRPCClientMode m_clientMode;
    uint32_t m_maxInflightCalls;
    uint64_t m_deadlineTimeoutMillis;

    // the stub
    std::unique_ptr<StubType> m_serviceStub;

    // synchronous stream mode members
    grpc::ClientContext* m_streamContext;
    ResponseType m_streamStatus;
    std::unique_ptr<grpc::ClientWriter<MessageType>> m_clientWriter;

    // asynchronous unary mode members
    grpc::CompletionQueue m_cq;

    // the reactor used for asynchronous callback streaming mode
    ELogGRPCBaseReactor<StubType, MessageType, ReceptorType>* m_reactor;

    /** @brief Sends a log record to a log target. */
    bool writeLogRecordUnary(const ELogRecord& logRecord, uint64_t& bytesWritten);
    bool writeLogRecordStream(const ELogRecord& logRecord, uint64_t& bytesWritten);
    bool writeLogRecordAsync(const ELogRecord& logRecord, uint64_t& bytesWritten);
    bool writeLogRecordAsyncCallbackUnary(const ELogRecord& logRecord, uint64_t& bytesWritten);
    bool writeLogRecordAsyncCallbackStream(const ELogRecord& logRecord, uint64_t& bytesWritten);

    // helper method to set single RPC call deadline
    inline void setDeadline(grpc::ClientContext& context) {
        std::chrono::time_point<std::chrono::system_clock> deadlineMillis =
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

/** @typedef Define the default GRPC log target type, using ELog protocol types. */
typedef ELogGRPCBaseTarget<> ELogGRPCTarget;

/** @brief Helper class for constructing specialized GRPC log target (used in target factory). */
class ELOG_API ELogGRPCBaseTargetConstructor {
public:
    virtual ~ELogGRPCBaseTargetConstructor() {}
    ELogGRPCBaseTargetConstructor(const ELogGRPCBaseTargetConstructor&) = delete;
    ELogGRPCBaseTargetConstructor(ELogGRPCBaseTargetConstructor&&) = delete;
    ELogGRPCBaseTargetConstructor& operator=(const ELogGRPCBaseTargetConstructor&) = delete;

    virtual ELogRpcTarget* createLogTarget(ELogReportHandler* reportHandler,
                                           const std::string& server, const std::string& params,
                                           const std::string& serverCA, const std::string& clientCA,
                                           const std::string& clientKey,
                                           ELogGRPCClientMode clientMode,
                                           uint64_t deadlineTimeoutMillis,
                                           uint32_t maxInflightCalls) = 0;

protected:
    ELogGRPCBaseTargetConstructor() {}
};

/**
 * @brief Helper function for registering GRPC target constructors.
 * @param name The name under which the target constructor is to be registered. This name is the
 * name to be used as the provider type in the log target configuration string.
 * @param targetConstructor The target constructor to register.
 */
extern void ELOG_API
registerGRPCTargetConstructor(const char* name, ELogGRPCBaseTargetConstructor* targetConstructor);

template <typename ServiceType, typename StubType, typename MessageType, typename ResponseType,
          typename ReceptorType>
class ELogGRPCTargetConstructor : public ELogGRPCBaseTargetConstructor {
public:
    ELogGRPCTargetConstructor() {}
    ELogGRPCTargetConstructor(const ELogGRPCTargetConstructor&) = delete;
    ELogGRPCTargetConstructor(ELogGRPCTargetConstructor&&) = delete;
    ELogGRPCTargetConstructor& operator=(const ELogGRPCTargetConstructor&) = delete;
    ~ELogGRPCTargetConstructor() final {}

    ELogRpcTarget* createLogTarget(ELogReportHandler* reportHandler, const std::string& server,
                                   const std::string& params, const std::string& serverCA,
                                   const std::string& clientCA, const std::string& clientKey,
                                   ELogGRPCClientMode clientMode, uint64_t deadlineTimeoutMillis,
                                   uint32_t maxInflightCalls) final {
        return new ELogGRPCBaseTarget<ServiceType, StubType, MessageType, ResponseType,
                                      ReceptorType>(reportHandler, server, params, serverCA,
                                                    clientCA, clientKey, clientMode,
                                                    deadlineTimeoutMillis, maxInflightCalls);
    }  // namespace elog
};

template <typename ServiceType, typename StubType, typename MessageType, typename ResponseType,
          typename ReceptorType = ELogGRPCReceptor>
class ELogGRPCTargetConstructorRegisterHelper {
public:
    ELogGRPCTargetConstructorRegisterHelper(const char* name) {
        registerGRPCTargetConstructor(name, &m_constructor);
    }
    ELogGRPCTargetConstructorRegisterHelper(const ELogGRPCTargetConstructorRegisterHelper&) =
        delete;
    ELogGRPCTargetConstructorRegisterHelper(ELogGRPCTargetConstructorRegisterHelper&&) = delete;
    ELogGRPCTargetConstructorRegisterHelper& operator=(
        const ELogGRPCTargetConstructorRegisterHelper&) = delete;
    ~ELogGRPCTargetConstructorRegisterHelper() {}

private:
    ELogGRPCTargetConstructor<ServiceType, StubType, MessageType, ResponseType, ReceptorType>
        m_constructor;
};

#define DECLARE_ELOG_GRPC_TARGET(ServiceType, MessageType, ResponseType, Name)                  \
    static ELogGRPCTargetConstructorRegisterHelper<ServiceType, ServiceType::Stub, MessageType, \
                                                   ResponseType>                                \
        Name##Register(#Name);

#define DECLARE_ELOG_GRPC_TARGET_EX(ServiceType, StubType, MessageType, ResponseType,  \
                                    ReceptorType, Name)                                \
    static ELogGRPCTargetConstructorRegisterHelper<ServiceType, StubType, MessageType, \
                                                   ResponseType, ReceptorType>         \
        Name##Register(#Name);

// actual usage scenarios:
// 1. using ELog gRPC as is, implementing server side as is like benchmark code
// for this use case we might consider adding server-side code fo easy integration
// 2. using ELog gRPC as part of another protocol. this requires adding protocol parts into user's
// protocol definition file. this also requires using templates on ELog's side, for the following
// types: stub, log record message, response status
// template classes:
//
// template <typename M = elog_grpc::ELogGRPCRecordMsg> class ELogGRPCBaseReceptor
// template <typename S = elog_grpc::ELogGRPCService::Stub, typename M =
// elog_grpc::ELogGRPCRecordMsg, typename R = elog_grpc::ELogGRPCStatus> class ELogGRPCBaseReactor
// template <typename S = elog_grpc::ELogGRPCService::Stub, typename M =
// elog_grpc::ELogGRPCRecordMsg, typename R = elog_grpc::ELogGRPCStatus> class ELogGRPCBaseTarget
//
// also the target provider will become a template.
// the default implementation over ELog predefined types will be registered in code (see macros).
//
// Now in case of completely different message, a new receptor should be written, to pass the fields
// into the message. Also if extra fields are used, then user should derive from receptor and handle
// extra fields. THIS SHOULD BE FIXED FOR ALL PLACES WHERE RECEPTOR IS USED
//
// finally, the log target should be registered, and this requires more infra code in the gRPC
// target provider, so it can register a specific implementation with a compile-time fixed name, and
// all this can be wrapped up in a single macro:
// ELOG_DECLARE_GRPC_TARGET(Stub, Message, Status, Name)

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

// IMPLEMENTATION

}  // namespace elog

#endif  // ELOG_ENABLE_GRPC_CONNECTOR

#endif  // __ELOG_GRPC_TARGET_H__

#include "elog_grpc_target.inl"