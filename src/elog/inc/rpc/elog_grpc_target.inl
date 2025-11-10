#ifndef __ELOG_GRPC_TARGET_INL__
#define __ELOG_GRPC_TARGET_INL__

#ifdef ELOG_ENABLE_GRPC_CONNECTOR

#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/support/interceptor.h>

#include <climits>

#define ELOG_INVALID_REQUEST_ID ((uint64_t)-1)
#define ELOG_FLUSH_REQUEST_ID ((uint64_t)-2)

namespace elog {

template <typename MessageType>
void ELogGRPCBaseReceptor<MessageType>::receiveStaticText(uint32_t typeId, const std::string& text,
                                                          const ELogFieldSpec& fieldSpec) {
    // static text is not used, just discard it
}

template <typename MessageType>
void ELogGRPCBaseReceptor<MessageType>::receiveRecordId(uint32_t typeId, uint64_t recordId,
                                                        const ELogFieldSpec& fieldSpec) {
    m_logRecordMsg->set_recordid(recordId);
}

template <typename MessageType>
void ELogGRPCBaseReceptor<MessageType>::receiveHostName(uint32_t typeId, const char* hostName,
                                                        const ELogFieldSpec& fieldSpec) {
    m_logRecordMsg->set_hostname(hostName);
}

template <typename MessageType>
void ELogGRPCBaseReceptor<MessageType>::receiveUserName(uint32_t typeId, const char* userName,
                                                        const ELogFieldSpec& fieldSpec) {
    m_logRecordMsg->set_username(userName);
}

template <typename MessageType>
void ELogGRPCBaseReceptor<MessageType>::receiveProgramName(uint32_t typeId, const char* programName,
                                                           const ELogFieldSpec& fieldSpec) {
    m_logRecordMsg->set_programname(programName);
}

template <typename MessageType>
void ELogGRPCBaseReceptor<MessageType>::receiveProcessId(uint32_t typeId, uint64_t processId,
                                                         const ELogFieldSpec& fieldSpec) {
    m_logRecordMsg->set_processid(processId);
}

template <typename MessageType>
void ELogGRPCBaseReceptor<MessageType>::receiveThreadId(uint32_t typeId, uint64_t threadId,
                                                        const ELogFieldSpec& fieldSpec) {
    m_logRecordMsg->set_threadid(threadId);
}

template <typename MessageType>
void ELogGRPCBaseReceptor<MessageType>::receiveThreadName(uint32_t typeId, const char* threadName,
                                                          const ELogFieldSpec& fieldSpec) {
    m_logRecordMsg->set_threadname(threadName);
}

template <typename MessageType>
void ELogGRPCBaseReceptor<MessageType>::receiveLogSourceName(uint32_t typeId,
                                                             const char* logSourceName,
                                                             const ELogFieldSpec& fieldSpec) {
    m_logRecordMsg->set_logsourcename(logSourceName);
}

template <typename MessageType>
void ELogGRPCBaseReceptor<MessageType>::receiveModuleName(uint32_t typeId, const char* moduleName,
                                                          const ELogFieldSpec& fieldSpec) {
    m_logRecordMsg->set_modulename(moduleName);
}

template <typename MessageType>
void ELogGRPCBaseReceptor<MessageType>::receiveFileName(uint32_t typeId, const char* fileName,
                                                        const ELogFieldSpec& fieldSpec) {
    m_logRecordMsg->set_file(fileName);
}

template <typename MessageType>
void ELogGRPCBaseReceptor<MessageType>::receiveLineNumber(uint32_t typeId, uint64_t lineNumber,
                                                          const ELogFieldSpec& fieldSpec) {
    m_logRecordMsg->set_line((uint32_t)lineNumber);
}

template <typename MessageType>
void ELogGRPCBaseReceptor<MessageType>::receiveFunctionName(uint32_t typeId,
                                                            const char* functionName,
                                                            const ELogFieldSpec& fieldSpec) {
    m_logRecordMsg->set_functionname(functionName);
}

template <typename MessageType>
void ELogGRPCBaseReceptor<MessageType>::receiveLogMsg(uint32_t typeId, const char* logMsg,
                                                      const ELogFieldSpec& fieldSpec) {
    m_logRecordMsg->set_logmsg(logMsg);
}

template <typename MessageType>
void ELogGRPCBaseReceptor<MessageType>::receiveStringField(uint32_t typeId, const char* value,
                                                           const ELogFieldSpec& fieldSpec,
                                                           size_t length) {
    // if external fields are used, then derive from the receptor and transfer the extra fields into
    // the log message
}

template <typename MessageType>
void ELogGRPCBaseReceptor<MessageType>::receiveIntField(uint32_t typeId, uint64_t value,
                                                        const ELogFieldSpec& fieldSpec) {
    // if external fields are used, then derive from the receptor and transfer the extra fields into
    // the log message
}

template <typename MessageType>
void ELogGRPCBaseReceptor<MessageType>::receiveTimeField(uint32_t typeId, const ELogTime& logTime,
                                                         const char* timeStr,
                                                         const ELogFieldSpec& fieldSpec,
                                                         size_t length) {
    uint64_t unixTimeMillis = elogTimeToUnixTimeNanos(logTime) / 1000000ULL;
    m_logRecordMsg->set_timeunixepochmillis(unixTimeMillis);
}

template <typename MessageType>
void ELogGRPCBaseReceptor<MessageType>::receiveLogLevelField(uint32_t typeId, ELogLevel logLevel,
                                                             const ELogFieldSpec& fieldSpec) {
    m_logRecordMsg->set_loglevel((uint32_t)logLevel);
}

// TODO: reactor is NOT USABLE after OnDone, and a new one must be used
// so in OnDone() we do "delete this" for the reactor
// once flush arrives the reactor should be "closed" to new pending messages and a new reactor
// should be made ready

template <typename StubType, typename MessageType, typename ReceptorType>
bool ELogGRPCBaseReactor<StubType, MessageType, ReceptorType>::writeLogRecord(
    const ELogRecord& logRecord, uint64_t& bytesWritten) {
    // this is thread-safe with respect to other calls to WriteLogRecord() and Flush(), but
    // not with respect to OnWriteDone() and onDone()

    // this must be done regardless of state
    CallData* callData = allocCallData();
    if (callData == nullptr) {
        m_reportHandler->onReport(m_logger, ELEVEL_ERROR, __FILE__, __LINE__, ELOG_FUNCTION,
                                  "Failed to allocate gRPC call data");
        return false;
    }
    m_rpcFormatter->fillInParams(logRecord, &callData->m_receptor);
    bytesWritten = callData->m_logRecordMsg->ByteSizeLong();

    ReactorState state = m_state.load(std::memory_order_acquire);
    if (state == ReactorState::RS_INIT) {
        // NOTE: there is no race with other write request (all writes and flush request are
        // serialized, either using a mutex or a queue)
        if (!m_state.compare_exchange_strong(state, ReactorState::RS_BATCH,
                                             std::memory_order_release)) {
            assert(false);
        }

        // at this point no OnWriteDone() or onDone() can arrive concurrently (as there is
        // no inflight message)
        if (m_reportHandler->isTraceEnabled()) {
            m_reportHandler->onReport(m_logger, ELEVEL_TRACE, __FILE__, __LINE__, ELOG_FUNCTION,
                                      "*** INIT --> BATCH, adding HOLD ***");
        }
        // NOTE: we need to add hold since there is a write flow that is outside of the reactor
        // (see documentation at
        // https://grpc.github.io/grpc/cpp/classgrpc_1_1_client_bidi_reactor.html)
        BaseClass::AddHold();
        // NOTE: there is no race here with other writes or flush, so we can safely change
        // in-flight and in-flight request id without the risk of facing any race conditions
        m_inFlight.store(true, std::memory_order_relaxed);
        uint64_t requestId = callData->m_requestId.m_atomicValue.load(std::memory_order_acquire);
        m_inFlightRequestId.store(requestId, std::memory_order_release);
        if (m_reportHandler->isTraceEnabled()) {
            std::string msg = "*** Set inflight (INIT) to true for request id ";
            msg += std::to_string(requestId);
            m_reportHandler->onReport(m_logger, ELEVEL_TRACE, __FILE__, __LINE__, ELOG_FUNCTION,
                                      msg.c_str());
        }
        BaseClass::StartWrite(callData->m_logRecordMsg);
        BaseClass::StartCall();  // this actually marks the start of a new RPC stream
    }

    // check other states
    else if (state == ReactorState::RS_BATCH) {
        bool inFlight = false;
        if (m_inFlight.compare_exchange_strong(inFlight, true, std::memory_order_acquire)) {
            // no message in flight, so we can just write it
            // NOTE: there is no race with other writers or flush, but rather only with
            // OnWriteDone(), but here we know that inflight is false, which means that
            // OnWriteDone() for previous message has already executed and reset the inflight flag
            // to false
            uint64_t requestId =
                callData->m_requestId.m_atomicValue.load(std::memory_order_acquire);
            m_inFlightRequestId.store(requestId, std::memory_order_release);
            if (m_reportHandler->isTraceEnabled()) {
                std::string msg = "*** Set inflight (BATCH) to true for request id ";
                msg += std::to_string(requestId);
                m_reportHandler->onReport(m_logger, ELEVEL_TRACE, __FILE__, __LINE__, ELOG_FUNCTION,
                                          msg.c_str());
            }
            BaseClass::StartWrite(callData->m_logRecordMsg);
        } else {
            // need to push on request queue and wait until in flight write request finishes
            // NOTE: we may have a race here with OnWriteDone() so we must use a lock
            std::unique_lock<std::mutex> lock(m_lock);
            uint64_t requestId =
                callData->m_requestId.m_atomicValue.load(std::memory_order_relaxed);
            m_pendingWriteRequests.push_front(requestId);
            if (m_reportHandler->isTraceEnabled()) {
                std::string msg =
                    "*** Inflight (BATCH) is already true, pushing pending request id ";
                msg += std::to_string(requestId);
                m_reportHandler->onReport(m_logger, ELEVEL_TRACE, __FILE__, __LINE__, ELOG_FUNCTION,
                                          msg.c_str());
            }
        }
    } else if (state == ReactorState::RS_FLUSH) {
        // this cannot happen, after FLUSH no incoming messages are allowed
        assert(false);
    }

    return true;
}

// TODO: make this lock-free implementation as "experimental" and add another with a proper lock

template <typename StubType, typename MessageType, typename ReceptorType>
void ELogGRPCBaseReactor<StubType, MessageType, ReceptorType>::flush() {
    if (m_reportHandler->isTraceEnabled()) {
        m_reportHandler->onReport(m_logger, ELEVEL_TRACE, __FILE__, __LINE__, ELOG_FUNCTION,
                                  "*** FLUSH ***");
    }
    // move to state flush
    // from this point until flush is done, no incoming requests are allowed
    // NOTE: move to state DONE will take place only after onDone() is called by gRPC
    setStateFlush();

    // we are in race here with gRPC notifications, so use a lock
    {
        std::unique_lock<std::mutex> lock(m_lock);
        bool inFlight = m_inFlight.load(std::memory_order_relaxed);
        if (inFlight || !m_pendingWriteRequests.empty()) {
            // flush request must be put in the queue, because there is in-flight message
            if (m_reportHandler->isTraceEnabled()) {
                std::string msg = "*** FLUSH request submitted, in-flight=";
                msg += inFlight ? "yes" : "no";
                m_reportHandler->onReport(m_logger, ELEVEL_TRACE, __FILE__, __LINE__, ELOG_FUNCTION,
                                          msg.c_str());
            }
            m_pendingWriteRequests.push_front(ELOG_FLUSH_REQUEST_ID);
            if (m_reportHandler->isTraceEnabled()) {
                std::string msg = "*** Pushed flush request on queue, inflight is ";
                msg += inFlight ? "true" : "false";
                m_reportHandler->onReport(m_logger, ELEVEL_TRACE, __FILE__, __LINE__, ELOG_FUNCTION,
                                          msg.c_str());
            }

            return;
        }
    }
    // NOTE: from this point onward we can safely say that there are no in-flight messages, the
    // pending queue is empty, and no message will be submitted to this stream (concurrent write
    // requests are blocked on ELogTarget's mutex, or are pending in some external queue, and by the
    // time they are served a new stream writer will be established), so don't need a lock here
    if (m_reportHandler->isTraceEnabled()) {
        m_reportHandler->onReport(m_logger, ELEVEL_TRACE, __FILE__, __LINE__, ELOG_FUNCTION,
                                  "*** FLUSH request starting, removing HOLD");
    }
    // NOTE: it is ok to call these two concurrently with OnWriteDone()
    BaseClass::StartWritesDone();
    // NOTE: since log target access is thread-safe, we can tell there will be no concurrent write
    // request until the reactor is regenerated, so we can remove the hold
    BaseClass::RemoveHold();
}

template <typename StubType, typename MessageType, typename ReceptorType>
bool ELogGRPCBaseReactor<StubType, MessageType, ReceptorType>::waitFlushDone() {
    std::unique_lock<std::mutex> lock(m_lock);
    m_cv.wait(lock, [this] {
        ReactorState state = m_state.load(std::memory_order_relaxed);
        return state == ReactorState::RS_DONE || state == ReactorState::RS_INIT;
    });
    return m_status.ok();
}

template <typename StubType, typename MessageType, typename ReceptorType>
void ELogGRPCBaseReactor<StubType, MessageType, ReceptorType>::OnWriteDone(bool ok) {
    // TODO: what if ok is false? currently we still continue, but we should at least issue some
    // trace
    if (!ok) {
        m_reportHandler->onReport(m_logger, ELEVEL_ERROR, __FILE__, __LINE__, ELOG_FUNCTION,
                                  "Single message stream write failed");
        // TODO: now what?
    }

    // reset in-flight flag (allow others to write new messages)
    bool inFlight = m_inFlight.load(std::memory_order_acquire);
    assert(inFlight);  // this must be the case!

    // get call data and free it (now it is safe to do so according to gRPC documentation)
    uint64_t requestId = m_inFlightRequestId.load(std::memory_order_relaxed);
    CallData* callData = &m_inFlightRequests[requestId % m_maxInflightCalls];
    callData->clear();

    if (m_reportHandler->isTraceEnabled()) {
        std::string msg = "*** OnWriteDone(): in-flight is true, completed request id ";
        msg += std::to_string(requestId);
        m_reportHandler->onReport(m_logger, ELEVEL_TRACE, __FILE__, __LINE__, ELOG_FUNCTION,
                                  msg.c_str());
    }

    // in order to maintain correct order, we do not reset yet the inflight flag,
    // but first, we check the pending queue
    requestId = ELOG_INVALID_REQUEST_ID;
    {
        std::unique_lock<std::mutex> lock(m_lock);
        if (!m_pendingWriteRequests.empty()) {
            requestId = m_pendingWriteRequests.back();
            m_pendingWriteRequests.pop_back();
        }
    }

    // NOTE: following code is thread-safe, see explanation in each case
    if (requestId == ELOG_FLUSH_REQUEST_ID) {
        // now we can end the batch (delayed flush execution)
        if (m_reportHandler->isTraceEnabled()) {
            m_reportHandler->onReport(m_logger, ELEVEL_TRACE, __FILE__, __LINE__, ELOG_FUNCTION,
                                      "*** Delayed FLUSH request starting, removing HOLD");
        }
        // these calls to gRPC are thread safe, since we use Holds
        // (See Q. 7 at best practices here: https://grpc.io/docs/languages/cpp/best_practices/)
        BaseClass::StartWritesDone();
        BaseClass::RemoveHold();
        // attention, since no incoming message is allowed after flush, we can surely assume that
        // all previous messages have been sent, and OnWriteDone() notification for all of them was
        // sent, so we can conclude that the pending messages queue is empty, and so the in-flight
        // flag can be reset back to false
        assert(m_pendingWriteRequests.empty());
        if (m_pendingWriteRequests.empty()) {
            if (!m_inFlight.compare_exchange_strong(inFlight, false, std::memory_order_release)) {
                // must succeed
                assert(false);
            }
        }
        // NOTE: if we don't set in-flight to false here, an assert will fire in OnDone(), seeing
        // that in-flight is still true (which is not expected during OnDone())
    } else if (requestId != ELOG_INVALID_REQUEST_ID) {
        // access to call data array is thread-safe from anywhere
        callData = &m_inFlightRequests[requestId % m_maxInflightCalls];
        if (m_reportHandler->isTraceEnabled()) {
            std::string msg = "*** OnWriteDone(): Starting write for delayed request id ";
            msg += std::to_string(requestId);
            m_reportHandler->onReport(m_logger, ELEVEL_TRACE, __FILE__, __LINE__, ELOG_FUNCTION,
                                      msg.c_str());
        }
        // must store the currently executed in-flight message request id, otherwise next round of
        // OnWriteDone() will not be able to find the call data and clean it up
        m_inFlightRequestId.store(requestId, std::memory_order_relaxed);
        // start write can be called outside reactor flow, since holds are used
        BaseClass::StartWrite(callData->m_logRecordMsg);
        // keep in-flight raised
    } else {
        // NOTE: access to in-flight flag IS thread-safe, because this is the only places where it
        // is set to false, so racing writers will see it as true, until it is set to false here
        if (m_reportHandler->isTraceEnabled()) {
            m_reportHandler->onReport(
                m_logger, ELEVEL_TRACE, __FILE__, __LINE__, ELOG_FUNCTION,
                "*** OnWriteDone(): No pending request, resetting inflight to false");
        }
        if (!m_inFlight.compare_exchange_strong(inFlight, false, std::memory_order_release)) {
            // must succeed
            assert(false);
        }
    }
}

template <typename StubType, typename MessageType, typename ReceptorType>
void ELogGRPCBaseReactor<StubType, MessageType, ReceptorType>::OnDone(const grpc::Status& status) {
    if (!status.ok()) {
        std::string errorMsg = "gRPC call (asynchronous callback stream) ended with error: ";
        errorMsg += status.error_message();
        m_reportHandler->onReport(m_logger, ELEVEL_ERROR, __FILE__, __LINE__, ELOG_FUNCTION,
                                  errorMsg.c_str());
    }

    if (m_reportHandler->isTraceEnabled()) {
        std::stringstream s;
        s << "*** OnDone(): state = " << (int)m_state.load(std::memory_order_relaxed)
          << ", in-flight=" << (m_inFlight ? "yes" : "no")
          << "pending-requests=" << m_pendingWriteRequests.size();
        m_reportHandler->onReport(m_logger, ELEVEL_TRACE, __FILE__, __LINE__, ELOG_FUNCTION,
                                  s.str().c_str());
    }

    // in order to avoid newcomers writing messages before the ones that needed to wait during
    // state FLUSH, we avoid moving to state INIT yet, until we check the queue state
    ReactorState state = m_state.load(std::memory_order_acquire);
    assert(state == ReactorState::RS_FLUSH);
    assert(m_inFlight.load(std::memory_order_relaxed) == false);

    // verify the queue is empty
    std::unique_lock<std::mutex> lock(m_lock);
    m_status = status;
    assert(m_pendingWriteRequests.empty());
    if (!m_state.compare_exchange_strong(state, ReactorState::RS_DONE, std::memory_order_release)) {
        assert(false);
    }
    if (m_reportHandler->isTraceEnabled()) {
        m_reportHandler->onReport(m_logger, ELEVEL_TRACE, __FILE__, __LINE__, ELOG_FUNCTION,
                                  "*** FLUSH --> DONE, FLUSH request executed");
    }
    m_cv.notify_one();
}

template <typename StubType, typename MessageType, typename ReceptorType>
ELogGRPCBaseReactor<StubType, MessageType, ReceptorType>::CallData::CallData()
    : m_requestId(((uint64_t)ELOG_INVALID_REQUEST_ID)), m_isUsed(false), m_logRecordMsg(nullptr) {}

template <typename StubType, typename MessageType, typename ReceptorType>
void ELogGRPCBaseReactor<StubType, MessageType, ReceptorType>::CallData::clear() {
    m_requestId.m_atomicValue.store(((uint64_t)ELOG_INVALID_REQUEST_ID), std::memory_order_relaxed);
    m_isUsed.m_atomicValue.store(false, std::memory_order_relaxed);
    // NOTE: the grpc framework does not take ownership of the message so we must delete it
    delete m_logRecordMsg;
    m_logRecordMsg = nullptr;
    m_receptor.setLogRecordMsg(nullptr);
}

template <typename StubType, typename MessageType, typename ReceptorType>
typename ELogGRPCBaseReactor<StubType, MessageType, ReceptorType>::CallData*
ELogGRPCBaseReactor<StubType, MessageType, ReceptorType>::allocCallData() {
    uint64_t requestId = m_nextRequestId.fetch_add(1, std::memory_order_relaxed);
    CallData* callData = &m_inFlightRequests[requestId % m_maxInflightCalls];
    bool isUsed = callData->m_isUsed.m_atomicValue.load(std::memory_order_relaxed);
    if (isUsed) {
        while (isUsed || !callData->m_isUsed.m_atomicValue.compare_exchange_strong(
                             isUsed, true, std::memory_order_seq_cst)) {
            // wait
            std::this_thread::yield();
            isUsed = callData->m_isUsed.m_atomicValue.load(std::memory_order_relaxed);
        }
    }
    if (!callData->init(requestId)) {
        m_reportHandler->onReport(m_logger, ELEVEL_ERROR, __FILE__, __LINE__, ELOG_FUNCTION,
                                  "Failed to allocate gRPC log record message, out of memory");
        callData->clear();
        return nullptr;
    }
    return callData;
}

template <typename StubType, typename MessageType, typename ReceptorType>
bool ELogGRPCBaseReactor<StubType, MessageType, ReceptorType>::setStateFlush() {
    ReactorState state = m_state.load(std::memory_order_acquire);
    if (state == ReactorState::RS_INIT) {
        // nothing to do, this is usually coming from a time flush policy when there are no
        // log records being written, so we simply discard the request
        return false;
    }
    if (state == ReactorState::RS_FLUSH) {
        // impossible to be already in flush state, flush can be called only once
        assert(false);
    }
    if (state != ReactorState::RS_BATCH) {
        // this is impossible, something bad happened (must be either INIT or BATCH)
        assert(false);
    }
    if (!m_state.compare_exchange_strong(state, ReactorState::RS_FLUSH,
                                         std::memory_order_release)) {
        // something is terribly wrong, unexpected race condition
        assert(false);
    }
    if (m_reportHandler->isTraceEnabled()) {
        m_reportHandler->onReport(m_logger, ELEVEL_TRACE, __FILE__, __LINE__, ELOG_FUNCTION,
                                  "*** BATCH --> FLUSH ***");
    }
    return false;
}

template <typename ServiceType, typename StubType, typename MessageType, typename ResponseType,
          typename ReceptorType>
bool ELogGRPCBaseTarget<ServiceType, StubType, MessageType, ResponseType,
                        ReceptorType>::startLogTarget() {
    // first let parent do initialization
    if (!ELogRpcTarget::startLogTarget()) {
        return false;
    }

    // parse the parameters with log record field selector tokens
    if (!parseParams(m_params)) {
        return false;
    }

    // create channel to server
    std::shared_ptr<grpc::Channel> channel;
    if (!m_serverCA.empty()) {
        grpc::SslCredentialsOptions sslOptions;
        sslOptions.pem_root_certs = m_serverCA;
        if (!m_clientCA.empty() && !m_clientKey.empty()) {
            sslOptions.pem_private_key = m_clientKey;
            sslOptions.pem_cert_chain = m_clientCA;
        }
        channel = grpc::CreateChannel(m_server.c_str(), grpc::SslCredentials(sslOptions));
    } else {
        channel = grpc::CreateChannel(m_server.c_str(), grpc::InsecureChannelCredentials());
    }

    // get the stub
    m_serviceStub = ServiceType::NewStub(channel);

    // stream mode requires more initialization
    if (m_clientMode == ELogGRPCClientMode::GRPC_CM_STREAM) {
        if (!createStreamContext()) {
            return false;
        }
        if (!createStreamWriter()) {
            destroyStreamContext();
            return false;
        }
    } else if (m_clientMode == ELogGRPCClientMode::GRPC_CM_ASYNC_CALLBACK_STREAM) {
        if (!createStreamContext()) {
            return false;
        }
        if (!createReactor()) {
            destroyStreamContext();
            return false;
        }
    }

    return true;
}

template <typename ServiceType, typename StubType, typename MessageType, typename ResponseType,
          typename ReceptorType>
bool ELogGRPCBaseTarget<ServiceType, StubType, MessageType, ResponseType,
                        ReceptorType>::stopLogTarget() {
    // for streaming clients we first flush all remaining messages
    // NOTE: we call directly flush code to bypass ELogTarget::flush()'s mutex since we already own
    // the lock
    if (m_clientMode == ELogGRPCClientMode::GRPC_CM_STREAM) {
        if (!flushStreamWriter()) {
            return false;
        }
        destroyStreamWriter();
        destroyStreamContext();
    } else if (m_clientMode == ELogGRPCClientMode::GRPC_CM_ASYNC_CALLBACK_STREAM) {
        if (!flushReactor()) {
            return false;
        }
        destroyReactor();
        destroyStreamContext();
    }

    // delete the stub
    m_serviceStub.reset();
    return true;
}

template <typename ServiceType, typename StubType, typename MessageType, typename ResponseType,
          typename ReceptorType>
bool ELogGRPCBaseTarget<ServiceType, StubType, MessageType, ResponseType,
                        ReceptorType>::writeLogRecord(const ELogRecord& logRecord,
                                                      uint64_t& bytesWritten) {
    // NOTE: we do not need to format the entire log msg

    // send message to gRPC server
    if (m_clientMode == ELogGRPCClientMode::GRPC_CM_UNARY) {
        return writeLogRecordUnary(logRecord, bytesWritten);
    } else if (m_clientMode == ELogGRPCClientMode::GRPC_CM_STREAM) {
        return writeLogRecordStream(logRecord, bytesWritten);
    } else if (m_clientMode == ELogGRPCClientMode::GRPC_CM_ASYNC) {
        return writeLogRecordAsync(logRecord, bytesWritten);
    } else if (m_clientMode == ELogGRPCClientMode::GRPC_CM_ASYNC_CALLBACK_UNARY) {
        return writeLogRecordAsyncCallbackUnary(logRecord, bytesWritten);
    } else if (m_clientMode == ELogGRPCClientMode::GRPC_CM_ASYNC_CALLBACK_STREAM) {
        return writeLogRecordAsyncCallbackStream(logRecord, bytesWritten);
    }

    std::string errorMsg = "Cannot write log record, invalid gRPC client mode: ";
    errorMsg += std::to_string((unsigned)m_clientMode);
    m_reportHandler->onReport(m_logger, ELEVEL_ERROR, __FILE__, __LINE__, ELOG_FUNCTION,
                              errorMsg.c_str());
    return 0;
}

template <typename ServiceType, typename StubType, typename MessageType, typename ResponseType,
          typename ReceptorType>
bool ELogGRPCBaseTarget<ServiceType, StubType, MessageType, ResponseType,
                        ReceptorType>::flushLogTarget() {
    // for non=streaming client no further operation is required

    bool res = true;
    if (m_clientMode == ELogGRPCClientMode::GRPC_CM_STREAM) {
        if (!flushStreamWriter()) {
            // TODO: what now?
            res = false;
        }
        destroyStreamWriter();
        destroyStreamContext();

        // regenerate context and client writer for next messages
        if (!createStreamContext()) {
            // TODO: sub-sequent writes will crash
            res = false;
        }
        if (!createStreamWriter()) {
            destroyStreamContext();
            // TODO: sub-sequent writes will crash (consider bad_alloc or even marking top-level
            // ELogTarget as unusable due ot unrecoverable error)
            res = false;
        }
    } else if (m_clientMode == ELogGRPCClientMode::GRPC_CM_ASYNC_CALLBACK_STREAM) {
        if (!flushReactor()) {
            // TODO: what now, should we modify flush() interface?
            res = false;
        }
        destroyReactor();
        destroyStreamContext();

        // regenerate context and reactor for next messages
        if (!createStreamContext()) {
            // TODO: sub-sequent writes will crash
            res = false;
        }
        if (!createReactor()) {
            destroyStreamContext();
            // TODO: sub-sequent writes will crash, this requires a uniform strategy (bad_alloc?)
            res = false;
        }
    }

    return res;
}

template <typename ServiceType, typename StubType, typename MessageType, typename ResponseType,
          typename ReceptorType>
bool ELogGRPCBaseTarget<ServiceType, StubType, MessageType, ResponseType,
                        ReceptorType>::writeLogRecordUnary(const ELogRecord& logRecord,
                                                           uint64_t& bytesWritten) {
    // prepare log record message
    // NOTE: receptor must live until message sending, because it holds value strings
    ELogGRPCBaseReceptor<> receptor;
    MessageType msg;
    receptor.setLogRecordMsg(&msg);
    fillInParams(logRecord, &receptor);

    // prepare context and set RPC call deadline
    grpc::ClientContext context;
    if (m_deadlineTimeoutMillis != 0) {
        setDeadline(context);
    }

    // send the message
    ResponseType status;
    grpc::Status callStatus = m_serviceStub->SendLogRecord(&context, msg, &status);
    if (!callStatus.ok()) {
        std::string errorMsg = "Failed to send log record over gRPC (synchronous unary): ";
        errorMsg += callStatus.error_message();
        static ELogModerate mod(errorMsg.c_str(), 1, ELOG_DEFAULT_ERROR_RATE_SECONDS,
                                ELogTimeUnits::TU_SECONDS);
        if (mod.moderate()) {
            m_reportHandler->onReport(m_logger, ELEVEL_ERROR, __FILE__, __LINE__, ELOG_FUNCTION,
                                      errorMsg.c_str());
        }
        // TODO: what now?
        return false;
    }

    bytesWritten = msg.ByteSizeLong();
    return true;
}

template <typename ServiceType, typename StubType, typename MessageType, typename ResponseType,
          typename ReceptorType>
bool ELogGRPCBaseTarget<ServiceType, StubType, MessageType, ResponseType,
                        ReceptorType>::writeLogRecordStream(const ELogRecord& logRecord,
                                                            uint64_t& bytesWritten) {
    // prepare log record message
    // NOTE: receptor must live until message sending, because it holds value strings
    ELogGRPCBaseReceptor<> receptor;
    MessageType msg;
    receptor.setLogRecordMsg(&msg);
    fillInParams(logRecord, &receptor);

    // NOTE: deadline already set once during stream construction

    // make sure we have a valid writer
    if (m_clientWriter.get() == nullptr) {
        // previous flush failed, we just silently drop the request
        return false;
    }

    // write next message in current RPC stream
    if (!m_clientWriter->Write(msg)) {
        const char* errorMsg = "Failed to stream log record over gRPC";
        static ELogModerate mod(errorMsg, 1, ELOG_DEFAULT_ERROR_RATE_SECONDS,
                                ELogTimeUnits::TU_SECONDS);
        if (mod.moderate()) {
            m_reportHandler->onReport(m_logger, ELEVEL_ERROR, __FILE__, __LINE__, ELOG_FUNCTION,
                                      errorMsg);
        }
        // TODO: what now?
        return false;
    }

    bytesWritten = msg.ByteSizeLong();
    return true;
}

template <typename ServiceType, typename StubType, typename MessageType, typename ResponseType,
          typename ReceptorType>
bool ELogGRPCBaseTarget<ServiceType, StubType, MessageType, ResponseType,
                        ReceptorType>::writeLogRecordAsync(const ELogRecord& logRecord,
                                                           uint64_t& bytesWritten) {
    // prepare log record message
    // NOTE: receptor must live until message sending, because it holds value strings
    ELogGRPCBaseReceptor<> receptor;
    MessageType msg;
    receptor.setLogRecordMsg(&msg);
    fillInParams(logRecord, &receptor);

    // prepare context and set RPC call deadline
    grpc::ClientContext context;
    if (m_deadlineTimeoutMillis != 0) {
        setDeadline(context);
    }

    // send a single async message
    std::unique_ptr<grpc::ClientAsyncResponseReader<ResponseType>> rpc =
        m_serviceStub->AsyncSendLogRecord(&context, msg, &m_cq);
    ResponseType status;
    grpc::Status callStatus;
    rpc->Finish(&status, &callStatus, (void*)1);

    // wait for message to finish
    // NOTE: although gRPC example do not clearly show that, it seems that the API implies we can
    // push more messages concurrently to the queue before response arrives. Nevertheless, the extra
    // effort is avoided, since this result is already achieved by the asynchronous callback stream
    // API, which is recommended by gRPC.
    void* tag = nullptr;
    bool ok = false;
    if (!m_cq.Next(&tag, &ok) || !ok) {
        const char* errorMsg = "Failed to get completion queue response in asynchronous mode gRPC";
        static ELogModerate mod(errorMsg, 1, ELOG_DEFAULT_ERROR_RATE_SECONDS,
                                ELogTimeUnits::TU_SECONDS);
        if (mod.moderate()) {
            m_reportHandler->onReport(m_logger, ELEVEL_ERROR, __FILE__, __LINE__, ELOG_FUNCTION,
                                      errorMsg);
        }
        // TODO: what now?
        return false;
    }

    if (tag != (void*)1) {
        const char* errorMsg = "Unexpected response tag in asynchronous mode gRPC";
        static ELogModerate mod(errorMsg, 1, ELOG_DEFAULT_ERROR_RATE_SECONDS,
                                ELogTimeUnits::TU_SECONDS);
        if (mod.moderate()) {
            m_reportHandler->onReport(m_logger, ELEVEL_ERROR, __FILE__, __LINE__, ELOG_FUNCTION,
                                      errorMsg);
        }
        return false;
    }

    if (!callStatus.ok()) {
        std::string errorMsg = "Asynchronous mode gRPC call ended with status FAIL: ";
        errorMsg += callStatus.error_message();
        static ELogModerate mod(errorMsg.c_str(), 1, ELOG_DEFAULT_ERROR_RATE_SECONDS,
                                ELogTimeUnits::TU_SECONDS);
        if (mod.moderate()) {
            m_reportHandler->onReport(m_logger, ELEVEL_ERROR, __FILE__, __LINE__, ELOG_FUNCTION,
                                      errorMsg.c_str());
        }
        return false;
    }

    bytesWritten = msg.ByteSizeLong();
    return true;
}

template <typename ServiceType, typename StubType, typename MessageType, typename ResponseType,
          typename ReceptorType>
bool ELogGRPCBaseTarget<ServiceType, StubType, MessageType, ResponseType,
                        ReceptorType>::writeLogRecordAsyncCallbackUnary(const ELogRecord& logRecord,
                                                                        uint64_t& bytesWritten) {
    // NOTE: receptor must live until message sending, because it holds value strings
    ELogGRPCBaseReceptor<> receptor;
    MessageType msg;
    receptor.setLogRecordMsg(&msg);
    fillInParams(logRecord, &receptor);

    // set call deadline
    grpc::ClientContext context;
    if (m_deadlineTimeoutMillis != 0) {
        setDeadline(context);
    }

    // should we wait until response arrives before sending the next log record?
    // this is the same question as with async completion queue
    // for this we need to add some pipeline mode, both for async queue and for async unary, but
    // that is what streaming is doing anyway, right? so what's the point?

    // NOTE: we need to wait for the result otherwise
    // the callback will access on-stack local objects that will already be invalid at the callback
    // invocation time, which may cause core dump, or even worse, memory overwrite
    ResponseType status;
    std::mutex responseLock;
    std::condition_variable responseCV;
    bool done = false;
    bool result = false;
    m_serviceStub->async()->SendLogRecord(
        &context, &msg, &status,
        [&responseLock, &responseCV, &done, &result](grpc::Status localStatus) {
            if (localStatus.ok()) {
                result = true;
            }
            std::lock_guard<std::mutex> lock(responseLock);
            done = true;
            responseCV.notify_one();
        });
    std::unique_lock<std::mutex> lock(responseLock);
    responseCV.wait(lock, [&done] { return done; });
    if (!result) {
        return false;
    }

    bytesWritten = msg.ByteSizeLong();
    return false;
}

template <typename ServiceType, typename StubType, typename MessageType, typename ResponseType,
          typename ReceptorType>
bool ELogGRPCBaseTarget<ServiceType, StubType, MessageType, ResponseType, ReceptorType>::
    writeLogRecordAsyncCallbackStream(const ELogRecord& logRecord, uint64_t& bytesWritten) {
    // make sure we have a valid reactor
    if (m_reactor == nullptr) {
        // previous flush failed, we just silently drop the request
        return false;
    }

    // NOTE: deadline already set once during stream construction

    // just pass on to the reactor to deal with it
    return m_reactor->writeLogRecord(logRecord, bytesWritten);
}

template <typename ServiceType, typename StubType, typename MessageType, typename ResponseType,
          typename ReceptorType>
bool ELogGRPCBaseTarget<ServiceType, StubType, MessageType, ResponseType,
                        ReceptorType>::createStreamContext() {
    m_streamContext = new (std::nothrow) grpc::ClientContext();
    if (m_streamContext == nullptr) {
        m_reportHandler->onReport(m_logger, ELEVEL_ERROR, __FILE__, __LINE__, ELOG_FUNCTION,
                                  "Failed to allocate gRPC stream context, out of memory");
        return false;
    }
    setDeadline(*m_streamContext);
    return true;
}

template <typename ServiceType, typename StubType, typename MessageType, typename ResponseType,
          typename ReceptorType>
void ELogGRPCBaseTarget<ServiceType, StubType, MessageType, ResponseType,
                        ReceptorType>::destroyStreamContext() {
    if (m_streamContext == nullptr) {
        delete m_streamContext;
        m_streamContext = nullptr;
    }
}

template <typename ServiceType, typename StubType, typename MessageType, typename ResponseType,
          typename ReceptorType>
bool ELogGRPCBaseTarget<ServiceType, StubType, MessageType, ResponseType,
                        ReceptorType>::createStreamWriter() {
    m_clientWriter = m_serviceStub->StreamLogRecords(m_streamContext, &m_streamStatus);
    if (m_clientWriter.get() == nullptr) {
        m_reportHandler->onReport(m_logger, ELEVEL_ERROR, __FILE__, __LINE__, ELOG_FUNCTION,
                                  "Failed to create gRPC synchronous streaming client writer");
        return false;
    }
    return true;
}

template <typename ServiceType, typename StubType, typename MessageType, typename ResponseType,
          typename ReceptorType>
bool ELogGRPCBaseTarget<ServiceType, StubType, MessageType, ResponseType,
                        ReceptorType>::flushStreamWriter() {
    m_clientWriter->WritesDone();
    grpc::Status callStatus = m_clientWriter->Finish();
    if (!callStatus.ok()) {
        std::string errorMsg =
            "Failed to terminate log record synchronous stream sending over gRPC: ";
        errorMsg += callStatus.error_message();
        m_reportHandler->onReport(m_logger, ELEVEL_ERROR, __FILE__, __LINE__, ELOG_FUNCTION,
                                  errorMsg.c_str());
        return false;
    }
    return true;
}

template <typename ServiceType, typename StubType, typename MessageType, typename ResponseType,
          typename ReceptorType>
void ELogGRPCBaseTarget<ServiceType, StubType, MessageType, ResponseType,
                        ReceptorType>::destroyStreamWriter() {
    m_clientWriter.reset(nullptr);
}

template <typename ServiceType, typename StubType, typename MessageType, typename ResponseType,
          typename ReceptorType>
bool ELogGRPCBaseTarget<ServiceType, StubType, MessageType, ResponseType,
                        ReceptorType>::createReactor() {
    m_reactor = new ELogGRPCBaseReactor<StubType, MessageType, ReceptorType>(
        m_reportHandler, m_serviceStub.get(), getRpcFormatter(), m_maxInflightCalls);
    if (m_reactor == nullptr) {
        m_reportHandler->onReport(m_logger, ELEVEL_ERROR, __FILE__, __LINE__, ELOG_FUNCTION,
                                  "Failed to allocate gRPC reactor, out of memory");
        return false;
    }
    m_serviceStub->async()->StreamLogRecords(m_streamContext, &m_streamStatus, m_reactor);
    return true;
}

template <typename ServiceType, typename StubType, typename MessageType, typename ResponseType,
          typename ReceptorType>
bool ELogGRPCBaseTarget<ServiceType, StubType, MessageType, ResponseType,
                        ReceptorType>::flushReactor() {
    m_reactor->flush();
    // we must for flush to finish properly, then regenerate reactor
    return !m_reactor->waitFlushDone();
}

template <typename ServiceType, typename StubType, typename MessageType, typename ResponseType,
          typename ReceptorType>
void ELogGRPCBaseTarget<ServiceType, StubType, MessageType, ResponseType,
                        ReceptorType>::destroyReactor() {
    if (m_reactor != nullptr) {
        delete m_reactor;
        m_reactor = nullptr;
    }
}

}  // namespace elog

#endif  // ELOG_ENABLE_GRPC_CONNECTOR

#endif  // __ELOG_GRPC_TARGET_INL__