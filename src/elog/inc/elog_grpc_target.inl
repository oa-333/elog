#ifndef __ELOG_GRPC_TARGET_INL__
#define __ELOG_GRPC_TARGET_INL__

#ifdef ELOG_ENABLE_GRPC_CONNECTOR

#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/support/interceptor.h>

#ifdef ELOG_MSVC
#define FILETIME_TO_UNIXTIME(ft) ((*(LONGLONG*)&(ft) - 116444736000000000LL) / 10000000LL)
#endif

#define ELOG_INVALID_REQUEST_ID ((uint64_t)-1)
#define ELOG_FLUSH_REQUEST_ID ((uint64_t)-2)

namespace elog {

template <typename MessageType>
void ELogGRPCBaseReceptor<MessageType>::receiveStringField(uint32_t typeId,
                                                           const std::string& value, int justify) {
    if (typeId == ELogHostNameSelector::getTypeId()) {
        m_logRecordMsg->set_hostname(value);
    } else if (typeId == ELogUserNameSelector::getTypeId()) {
        m_logRecordMsg->set_username(value);
    } else if (typeId == ELogProgramNameSelector::getTypeId()) {
        m_logRecordMsg->set_programname(value);
    } else if (typeId == ELogThreadNameSelector::getTypeId()) {
        m_logRecordMsg->set_threadname(value);
    } else if (typeId == ELogSourceSelector::getTypeId()) {
        m_logRecordMsg->set_logsourcename(value);
    } else if (typeId == ELogModuleSelector::getTypeId()) {
        m_logRecordMsg->set_modulename(value);
    } else if (typeId == ELogFileSelector::getTypeId()) {
        m_logRecordMsg->set_file(value);
    } else if (typeId == ELogFunctionSelector::getTypeId()) {
        m_logRecordMsg->set_functionname(value);
    } else if (typeId == ELogMsgSelector::getTypeId()) {
        m_logRecordMsg->set_logmsg(value);
    }
    // if external fields are used, then derive from the receptor and transfer the extra fields into
    // the log message
}

template <typename MessageType>
void ELogGRPCBaseReceptor<MessageType>::receiveIntField(uint32_t typeId, uint64_t value,
                                                        int justify) {
    if (typeId == ELogRecordIdSelector::getTypeId()) {
        m_logRecordMsg->set_recordid(value);
    } else if (typeId == ELogProcessIdSelector::getTypeId()) {
        m_logRecordMsg->set_processid(value);
    } else if (typeId == ELogThreadIdSelector::getTypeId()) {
        m_logRecordMsg->set_threadid(value);
    } else if (typeId == ELogLineSelector::getTypeId()) {
        m_logRecordMsg->set_line((uint32_t)value);
    }
    // if external fields are used, then derive from the receptor and transfer the extra fields into
    // the log message
}

#ifdef ELOG_MSVC
template <typename MessageType>
void ELogGRPCBaseReceptor<MessageType>::receiveTimeField(uint32_t typeId, const SYSTEMTIME& sysTime,
                                                         const char* timeStr, int justify) final {
    FILETIME fileTime;
    SystemTimeToFileTime(&sysTime, fileTime);
    uint64_t utcTimeMillis = (uint64_t)FILETIME_TO_UNIXTIME(fileTime);
    m_logRecordMsg->set_timeutcmillis(utcTimeMillis);
}
#else
template <typename MessageType>
void ELogGRPCBaseReceptor<MessageType>::receiveTimeField(uint32_t typeId, const timeval& sysTime,
                                                         const char* timeStr, int justify) {
    uint64_t utcTimeMillis = sysTime.tv_sec * 1000 + sysTime.tv_usec / 1000;
    m_logRecordMsg->set_timeutcmillis(utcTimeMillis);
}
#endif

template <typename MessageType>
void ELogGRPCBaseReceptor<MessageType>::receiveLogLevelField(uint32_t typeId, ELogLevel logLevel,
                                                             int justify) {
    const char* logLevelStr = elogLevelToStr(logLevel);
    m_logRecordMsg->set_loglevel((uint32_t)logLevel);
}

// TODO: reactor is NOT USABLE after OnDone, and a new one must be used
// so in OnDone() we do "delete this" for the reactor
// once flush arrives the reactor should be "closed" to new pending messages and a new reactor
// should be made ready

template <typename StubType, typename MessageType, typename ReceptorType>
uint32_t ELogGRPCBaseReactor<StubType, MessageType, ReceptorType>::writeLogRecord(
    const ELogRecord& logRecord) {
    // this is thread-safe with respect to other calls to WriteLogRecord() and Flush(), but
    // not with respect to OnWriteDone() and onDone()

    // this must be done regardless of state
    // TODO: allocate data in ring buffer and assign id for fast locating
    CallData* callData = allocCallData();
    if (callData == nullptr) {
        m_errorHandler->onError("Failed to allocate gRPC call data");
        return 0;
    }
    m_rpcFormatter->fillInParams(logRecord, &callData->m_receptor);
    uint32_t bytesWritten = callData->m_logRecordMsg->ByteSizeLong();

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
        if (m_errorHandler->isTraceEnabled()) {
            m_errorHandler->onTrace("*** INIT --> BATCH, adding HOLD ***");
        }
        // NOTE: we need to add hold since there is a write flow that is outside of the reactor
        // (see documentation at
        // https://grpc.github.io/grpc/cpp/classgrpc_1_1_client_bidi_reactor.html)
        BaseClass::AddHold();
        // NOTE: there is no race here with other writes or flush, so we can safely change
        // in-flight and in-flight request id without the risk of facing any race conditions
        m_inFlight.store(true, std::memory_order_relaxed);
        m_inFlightRequestId.store(
            callData->m_requestId.m_atomicValue.load(std::memory_order_relaxed),
            std::memory_order_relaxed);
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
            m_inFlightRequestId.store(
                callData->m_requestId.m_atomicValue.load(std::memory_order_relaxed),
                std::memory_order_release);
            BaseClass::StartWrite(callData->m_logRecordMsg);
        } else {
            // need to push on request queue and wait until in flight write request finishes
            // NOTE: we may have a race here with OnWriteDone() so we must use a lock
            std::unique_lock<std::mutex> lock(m_lock);
            m_pendingWriteRequests.push_front(
                callData->m_requestId.m_atomicValue.load(std::memory_order_relaxed));
        }
    } else if (state == ReactorState::RS_FLUSH) {
        // this cannot happen, after FLUSH no incoming messages are allowed
        assert(false);
    }

    return bytesWritten;
}

// TODO: make this lock-free implementation as "experimental" and add another with a proper lock

template <typename StubType, typename MessageType, typename ReceptorType>
void ELogGRPCBaseReactor<StubType, MessageType, ReceptorType>::flush() {
    if (m_errorHandler->isTraceEnabled()) {
        m_errorHandler->onTrace("*** FLUSH ***");
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
            if (m_errorHandler->isTraceEnabled()) {
                std::string traceMsg = "*** FLUSH request submitted (in-flight=%s)";
                traceMsg += inFlight ? "yes" : "no";
                m_errorHandler->onTrace(traceMsg.c_str());
            }
            m_pendingWriteRequests.push_front(ELOG_FLUSH_REQUEST_ID);
            return;
        }
    }
    // NOTE: from this point onward we can safely say that there are no in-flight messages, the
    // pending queue is empty, and no message will be submitted to this stream (concurrent write
    // requests are blocked on ELogTarget's mutex, or are pending in some external queue, and by the
    // time they are served a new stream writer will be established), so don't need a lock here
    if (m_errorHandler->isTraceEnabled()) {
        m_errorHandler->onTrace("*** FLUSH request starting, removing HOLD");
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
        m_errorHandler->onError("Single message stream write failed");
        // TODO: now what?
    }

    // reset in-flight flag (allow others to write new messages)
    bool inFlight = m_inFlight.load(std::memory_order_acquire);
    assert(inFlight);  // this must be the case!

    // get call data and free it (now it is safe to do so according to gRPC documentation)
    uint64_t requestId = m_inFlightRequestId.load(std::memory_order_relaxed);
    CallData* callData = &m_inFlightRequests[requestId % m_inFlightRequests.size()];
    callData->clear();

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
        if (m_errorHandler->isTraceEnabled()) {
            m_errorHandler->onTrace("*** Delayed FLUSH request starting, removing HOLD");
        }
        // these calls to gRPC are thread safe, since we use Holds
        // (See Q. 7 at best practices here: https://grpc.io/docs/languages/cpp/best_practices/)
        BaseClass::StartWritesDone();
        BaseClass::RemoveHold();
    } else if (requestId != ELOG_INVALID_REQUEST_ID) {
        // access to call data array is thread-safe from anywhere
        callData = &m_inFlightRequests[requestId % m_inFlightRequests.size()];
        // start write can be called outside reactor flow, since holds are used
        BaseClass::StartWrite(callData->m_logRecordMsg);
        // keep in-flight raised
    } else {
        // NOTE: access to in-flight flag IS thread-safe, because this is the only places where it
        // is set to false, so racing writers will see it as true, until it is set to false here
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
        m_errorHandler->onError(errorMsg.c_str());
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
    if (m_errorHandler->isTraceEnabled()) {
        m_errorHandler->onTrace("*** FLUSH --> DONE, FLUSH request executed");
    }
    m_cv.notify_one();
}

template <typename StubType, typename MessageType, typename ReceptorType>
ELogGRPCBaseReactor<StubType, MessageType, ReceptorType>::CallData*
ELogGRPCBaseReactor<StubType, MessageType, ReceptorType>::allocCallData() {
    uint64_t requestId = m_nextRequestId.fetch_add(1, std::memory_order_relaxed);
    CallData* callData = &m_inFlightRequests[requestId % m_inFlightRequests.size()];
    bool isUsed = callData->m_isUsed.m_atomicValue.load(std::memory_order::relaxed);
    if (isUsed) {
        while (isUsed || !callData->m_isUsed.m_atomicValue.compare_exchange_strong(
                             isUsed, true, std::memory_order_seq_cst)) {
            // wait
            std::this_thread::yield();
            isUsed = callData->m_isUsed.m_atomicValue.load(std::memory_order::relaxed);
        }
    }
    if (!callData->init(requestId)) {
        m_errorHandler->onError("Failed to allocate gRPC log record message, out of memory");
        callData->clear();
        return nullptr;
    }
    return callData;
}

template <typename StubType, typename MessageType, typename ReceptorType>
bool ELogGRPCBaseReactor<StubType, MessageType, ReceptorType>::setStateFlush() {
    ReactorState state = m_state.load(std::memory_order_acquire);
    if (state == ReactorState::RS_INIT) {
        // nothing to do, this is usually coming from a timed flush policy when there are no
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
    if (m_errorHandler->isTraceEnabled()) {
        m_errorHandler->onTrace("*** BATCH --> FLUSH ***");
    }
    return false;
}

template <typename ServiceType, typename StubType, typename MessageType, typename ResponseType,
          typename ReceptorType>
bool ELogGRPCBaseTarget<ServiceType, StubType, MessageType, ResponseType,
                        ReceptorType>::startLogTarget() {
    // parse the parameters with log record field selector tokens
    if (!parseParams(m_params)) {
        return false;
    }

    // create channel to server
    std::shared_ptr<grpc::Channel> channel =
        grpc::CreateChannel(m_server.c_str(), grpc::InsecureChannelCredentials());

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
uint32_t ELogGRPCBaseTarget<ServiceType, StubType, MessageType, ResponseType,
                            ReceptorType>::writeLogRecord(const ELogRecord& logRecord) {
    // NOTE: we do not need to format the entire log msg

    // send message to gRPC server
    if (m_clientMode == ELogGRPCClientMode::GRPC_CM_UNARY) {
        return writeLogRecordUnary(logRecord);
    } else if (m_clientMode == ELogGRPCClientMode::GRPC_CM_STREAM) {
        return writeLogRecordStream(logRecord);
    } else if (m_clientMode == ELogGRPCClientMode::GRPC_CM_ASYNC) {
        return writeLogRecordAsync(logRecord);
    } else if (m_clientMode == ELogGRPCClientMode::GRPC_CM_ASYNC_CALLBACK_UNARY) {
        return writeLogRecordAsyncCallbackUnary(logRecord);
    } else if (m_clientMode == ELogGRPCClientMode::GRPC_CM_ASYNC_CALLBACK_STREAM) {
        return writeLogRecordAsyncCallbackStream(logRecord);
    }

    std::string errorMsg = "Cannot write log record, invalid gRPC client mode: ";
    errorMsg += std::to_string((unsigned)m_clientMode);
    m_errorHandler->onError(errorMsg.c_str());
    return 0;
}

template <typename ServiceType, typename StubType, typename MessageType, typename ResponseType,
          typename ReceptorType>
void ELogGRPCBaseTarget<ServiceType, StubType, MessageType, ResponseType,
                        ReceptorType>::flushLogTarget() {
    // for non=streaming client no further operation is required

    if (m_clientMode == ELogGRPCClientMode::GRPC_CM_STREAM) {
        if (!flushStreamWriter()) {
            // TODO: what now?
        }
        destroyStreamWriter();
        destroyStreamContext();

        // regenerate context and client writer for next messages
        if (!createStreamContext()) {
            // TODO: sub-sequent writes will crash
            return;
        }
        if (!createStreamWriter()) {
            destroyStreamContext();
            // TODO: sub-sequent writes will crash (consider bad_alloc or even marking top-level
            // ELogTarget as unusable due ot unrecoverable error)
            return;
        }
    } else if (m_clientMode == ELogGRPCClientMode::GRPC_CM_ASYNC_CALLBACK_STREAM) {
        if (!flushReactor()) {
            // TODO: what now, should we modify flush() interface?
        }
        destroyReactor();
        destroyStreamContext();

        // regenerate context and reactor for next messages
        if (!createStreamContext()) {
            // TODO: sub-sequent writes will crash
            return;
        }
        if (!createReactor()) {
            destroyStreamContext();
            // TODO: sub-sequent writes will crash, this requires a uniform strategy (bad_alloc?)
            return;
        }
    }
}

template <typename ServiceType, typename StubType, typename MessageType, typename ResponseType,
          typename ReceptorType>
uint32_t ELogGRPCBaseTarget<ServiceType, StubType, MessageType, ResponseType,
                            ReceptorType>::writeLogRecordUnary(const ELogRecord& logRecord) {
    // prepare log record message
    // NOTE: receptor must live until message sending, because it holds value strings
    ELogGRPCBaseReceptor receptor;
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
        m_errorHandler->onError(errorMsg.c_str());
        // TODO: what now?
        return 0;
    } else {
        return msg.ByteSizeLong();
    }
}

template <typename ServiceType, typename StubType, typename MessageType, typename ResponseType,
          typename ReceptorType>
uint32_t ELogGRPCBaseTarget<ServiceType, StubType, MessageType, ResponseType,
                            ReceptorType>::writeLogRecordStream(const ELogRecord& logRecord) {
    // prepare log record message
    // NOTE: receptor must live until message sending, because it holds value strings
    ELogGRPCBaseReceptor receptor;
    MessageType msg;
    receptor.setLogRecordMsg(&msg);
    fillInParams(logRecord, &receptor);

    // NOTE: deadline already set once during stream construction

    // make sure we have a valid writer
    if (m_clientWriter.get() == nullptr) {
        // previous flush failed, we just silently drop the request
        return 0;
    }

    // write next message in current RPC stream
    if (!m_clientWriter->Write(msg)) {
        m_errorHandler->onError("Failed to stream log record over gRPC");
        // TODO: what now?
        return 0;
    } else {
        // TODO: fix this: we need to separate submitted/written counters
        return msg.ByteSizeLong();
    }
}

template <typename ServiceType, typename StubType, typename MessageType, typename ResponseType,
          typename ReceptorType>
uint32_t ELogGRPCBaseTarget<ServiceType, StubType, MessageType, ResponseType,
                            ReceptorType>::writeLogRecordAsync(const ELogRecord& logRecord) {
    // prepare log record message
    // NOTE: receptor must live until message sending, because it holds value strings
    ELogGRPCBaseReceptor receptor;
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
        m_errorHandler->onError(
            "Failed to get completion queue response in asynchronous mode gRPC");
        // TODO: what now?
        return 0;
    } else if (tag != (void*)1) {
        m_errorHandler->onError("Unexpected response tag in asynchronous mode gRPC");
        return 0;
    } else if (!callStatus.ok()) {
        std::string errorMsg = "Asynchronous mode gRPC call ended with status FAIL: ";
        errorMsg += callStatus.error_message();
        m_errorHandler->onError(errorMsg.c_str());
        return 0;
    } else {
        return msg.ByteSizeLong();
    }
}

template <typename ServiceType, typename StubType, typename MessageType, typename ResponseType,
          typename ReceptorType>
uint32_t
ELogGRPCBaseTarget<ServiceType, StubType, MessageType, ResponseType,
                   ReceptorType>::writeLogRecordAsyncCallbackUnary(const ELogRecord& logRecord) {
    // NOTE: receptor must live until message sending, because it holds value strings
    ELogGRPCBaseReceptor receptor;
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
        &context, &msg, &status, [&responseLock, &responseCV, &done, &result](grpc::Status status) {
            if (status.ok()) {
                result = true;
            }
            std::lock_guard<std::mutex> lock(responseLock);
            done = true;
            responseCV.notify_one();
        });
    std::unique_lock<std::mutex> lock(responseLock);
    responseCV.wait(lock, [&done] { return done; });
    if (result) {
        return msg.ByteSizeLong();
    } else {
        return 0;
    }
}

template <typename ServiceType, typename StubType, typename MessageType, typename ResponseType,
          typename ReceptorType>
uint32_t
ELogGRPCBaseTarget<ServiceType, StubType, MessageType, ResponseType,
                   ReceptorType>::writeLogRecordAsyncCallbackStream(const ELogRecord& logRecord) {
    // make sure we have a valid reactor
    if (m_reactor == nullptr) {
        // previous flush failed, we just silently drop the request
        return 0;
    }

    // NOTE: deadline already set once during stream construction

    // just pass on to the reactor to deal with it
    return m_reactor->writeLogRecord(logRecord);
}

template <typename ServiceType, typename StubType, typename MessageType, typename ResponseType,
          typename ReceptorType>
bool ELogGRPCBaseTarget<ServiceType, StubType, MessageType, ResponseType,
                        ReceptorType>::createStreamContext() {
    m_streamContext = new (std::nothrow) grpc::ClientContext();
    if (m_streamContext == nullptr) {
        m_errorHandler->onError("Failed to allocate gRPC stream context, out of memory");
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
        m_errorHandler->onError("Failed to create gRPC synchronous streaming client writer");
        return false;
    }
    return m_clientWriter.get() != nullptr;
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
        m_errorHandler->onError(errorMsg.c_str());
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
        m_errorHandler, m_serviceStub.get(), getRpcFormatter(), m_maxInflightCalls);
    if (m_reactor == nullptr) {
        m_errorHandler->onError("Failed to allocate gRPC reactor, out of memory");
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