#include "elog_grpc_target.h"

#ifdef ELOG_ENABLE_GRPC_CONNECTOR

#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/support/interceptor.h>

#include "elog_error.h"
#include "elog_field_selector_internal.h"

#ifdef ELOG_MSVC
#define FILETIME_TO_UNIXTIME(ft) ((*(LONGLONG*)&(ft) - 116444736000000000LL) / 10000000LL)
#endif

namespace elog {

void ELogGRPCFieldReceptor::receiveStringField(uint32_t typeId, const std::string& value,
                                               int justify) {
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
    // TODO: what about external fields?
}

void ELogGRPCFieldReceptor::receiveIntField(uint32_t typeId, uint64_t value, int justify) {
    if (typeId == ELogRecordIdSelector::getTypeId()) {
        m_logRecordMsg->set_recordid(value);
    } else if (typeId == ELogProcessIdSelector::getTypeId()) {
        m_logRecordMsg->set_processid(value);
    } else if (typeId == ELogThreadIdSelector::getTypeId()) {
        m_logRecordMsg->set_threadid(value);
    } else if (typeId == ELogLineSelector::getTypeId()) {
        m_logRecordMsg->set_line((uint32_t)value);
    }
    // TODO: what about external fields?
}

#ifdef ELOG_MSVC
void ELogGRPCFieldReceptor::receiveTimeField(uint32_t typeId, const SYSTEMTIME& sysTime,
                                             const char* timeStr, int justify) final {
    FILETIME fileTime;
    SystemTimeToFileTime(&sysTime, fileTime);
    uint64_t utcTimeMillis = (uint64_t)FILETIME_TO_UNIXTIME(fileTime);
    m_logRecordMsg->set_timeutcmillis(utcTimeMillis);
}
#else
void ELogGRPCFieldReceptor::receiveTimeField(uint32_t typeId, const timeval& sysTime,
                                             const char* timeStr, int justify) {
    uint64_t utcTimeMillis = sysTime.tv_sec * 1000 + sysTime.tv_usec / 1000;
    m_logRecordMsg->set_timeutcmillis(utcTimeMillis);
}
#endif

void ELogGRPCFieldReceptor::receiveLogLevelField(uint32_t typeId, ELogLevel logLevel, int justify) {
    const char* logLevelStr = elogLevelToStr(logLevel);
    m_logRecordMsg->set_loglevel((uint32_t)logLevel);
}

// TODO: reactor is NOT USABLE after OnDone, and a new one must be used
// so in OnDone() we do "delete this" for the reactor
// once flush arrives the reactor should be "closed" to new pending messages and a new reactor
// should be made ready

uint32_t ELogReactor::writeLogRecord(const ELogRecord& logRecord) {
    // this is thread-safe with respect to other calls to WriteLogRecord() and Flush(), but
    // not with respect to OnWriteDone() and onDone()

    // this must be done regardless of state
    // TODO: allocate data in ring buffer and assign id for fast locating
    CallData* callData = allocCallData();
    assert(callData != nullptr);
    m_rpcFormatter->fillInParams(logRecord, &callData->m_receptor);
    uint32_t bytesWritten = callData->m_logRecordMsg->ByteSizeLong();

    ReactorState state = m_state.load(std::memory_order_acquire);
    if (state == ReactorState::RS_INIT) {
        // at this point no OnWriteDone() or onDone() can arrive concurrently (as there is
        // no inflight message)
        if (!m_state.compare_exchange_strong(state, ReactorState::RS_BATCH,
                                             std::memory_order_release)) {
            assert(false);
        }
        ELOG_REPORT_TRACE("*** INIT --> BATCH, adding HOLD ***");
        AddHold();
        m_inFlight.store(true, std::memory_order_relaxed);
        m_inFlightRequestId.store(
            callData->m_requestId.m_atomicValue.load(std::memory_order_relaxed),
            std::memory_order_relaxed);
        StartWrite(callData->m_logRecordMsg);
        StartCall();  // this actually marks the start of a new stream
    } else if (state == ReactorState::RS_BATCH) {
        bool inFlight = false;
        if (m_inFlight.compare_exchange_strong(inFlight, true, std::memory_order_acquire)) {
            // no message in flight, so we can just write it
            // NOTE: there is no race with other writers or flush, but rather only with
            // OnWriteDone() but inflight is false, so OnWriteDone for last message has already
            // executed and reset the inflight flag to false
            m_inFlightRequestId.store(
                callData->m_requestId.m_atomicValue.load(std::memory_order_relaxed),
                std::memory_order_release);
            StartWrite(callData->m_logRecordMsg);
        } else {
            // need to push on request queue and wait until in flight write request finishes
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

void ELogReactor::flush() {
    ELOG_REPORT_TRACE("*** FLUSH ***");
    // move to state flush
    // from this point until flush is done, no incoming requests are allowed
    setStateFlush();

    // we are in race here with gRPC notifications, so use a lock
    std::unique_lock<std::mutex> lock(m_lock);
    bool inFlight = m_inFlight.load(std::memory_order_relaxed);
    if (inFlight) {
        // flush request must be put in the queue, because there is in-flight message
        ELOG_REPORT_TRACE("*** FLUSH request submitted (in-flight=%s)", inFlight ? "yes" : "no");
        m_pendingWriteRequests.push_front(-2);
        return;
    }

    // neither in-flight message nor previous flush is still in-flight, so we can make the call
    // NOTE: move to state DONE will take place only after onDone() is called by gRPC
    ELOG_REPORT_TRACE("*** FLUSH request starting, removing HOLD");
    StartWritesDone();
    RemoveHold();
}

void ELogReactor::waitFlushDone() {
    std::unique_lock<std::mutex> lock(m_lock);
    m_cv.wait(lock, [this] {
        ReactorState state = m_state.load(std::memory_order_relaxed);
        return state == ReactorState::RS_DONE || state == ReactorState::RS_INIT;
    });
}

void ELogReactor::OnWriteDone(bool ok) {
    // reset in-flight flag (allow others to write new messages)
    bool inFlight = m_inFlight.load(std::memory_order_acquire);
    assert(inFlight);  // this must be the case!

    // get call data and free it
    uint64_t requestId = m_inFlightRequestId.load(std::memory_order_relaxed);
    CallData* callData = &m_inFlightRequests[requestId % m_inFlightRequests.size()];
    callData->clear();

    // in order to maintain correct order, we do not reset yet the inflight flag,
    // but first, we check the pending queue
    requestId = -1;
    {
        std::unique_lock<std::mutex> lock(m_lock);
        if (!m_pendingWriteRequests.empty()) {
            requestId = m_pendingWriteRequests.back();
            m_pendingWriteRequests.pop_back();
        }
    }
    // TODO: verify we can do this out of lock scope
    if (requestId == -2) {
        // now we can end the batch (delayed flush execution)
        ELOG_REPORT_TRACE("*** Delayed FLUSH request starting, removing HOLD");
        StartWritesDone();
        RemoveHold();
    } else if (requestId != -1) {
        callData = &m_inFlightRequests[requestId % m_inFlightRequests.size()];
        StartWrite(callData->m_logRecordMsg);
        // keep in-flight raised
    } else {
        if (!m_inFlight.compare_exchange_strong(inFlight, false, std::memory_order_release)) {
            // must succeed
            assert(false);
        }
    }
}

void ELogReactor::OnDone(const grpc::Status& s) {
    // in order to avoid newcomers writing messages before the ones that needed to wait during
    // state FLUSH, we avoid moving to state INIT yet, until we check the queue state
    ReactorState state = m_state.load(std::memory_order_acquire);
    assert(state == ReactorState::RS_FLUSH);

    // verify the queue is empty
    std::unique_lock<std::mutex> lock(m_lock);
    assert(m_pendingWriteRequests.empty());
    if (!m_state.compare_exchange_strong(state, ReactorState::RS_DONE, std::memory_order_release)) {
        assert(false);
    }
    ELOG_REPORT_TRACE("*** FLUSH --> DONE, FLUSH request executed");
    m_cv.notify_one();
}

ELogReactor::CallData* ELogReactor::allocCallData() {
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
    callData->init(requestId);
    return callData;
}

bool ELogReactor::setStateFlush() {
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
    ELOG_REPORT_TRACE("*** BATCH --> FLUSH ***");
    return false;
}

void ELogReactor::setStateInit() {
    ReactorState state = m_state.load(std::memory_order_acquire);
    if (state != ReactorState::RS_FLUSH) {
        // this is impossible, something bad happened
        assert(false);
    }
    if (!m_state.compare_exchange_strong(state, ReactorState::RS_INIT, std::memory_order_release)) {
        // something is terribly wrong
        assert(false);
    }
    ELOG_REPORT_TRACE("*** FLUSH --> INIT ***");
}

bool ELogGRPCTarget::startLogTarget() {
    // parse the parameters with log record field selector tokens
    if (!parseParams(m_params)) {
        return false;
    }

    // create channel to server
    // TODO: are there any exceptions?
    // TODO: what about timeouts?
    std::shared_ptr<grpc::Channel> channel =
        grpc::CreateChannel(m_server.c_str(), grpc::InsecureChannelCredentials());

    // get the stub
    m_serviceStub = elog_grpc::ELogGRPCService::NewStub(channel);

    if (m_clientMode == ELogGRPCClientMode::GRPC_CM_STREAM) {
        m_clientWriter = m_serviceStub->StreamLogRecords(&m_streamContext, &m_streamStatus);
    } else if (m_clientMode == ELogGRPCClientMode::GRPC_CM_ASYNC_CALLBACK_STREAM) {
        m_reactor = new (std::nothrow)
            ELogReactor(m_serviceStub.get(), getRpcFormatter(), m_maxInflightCalls);
        m_serviceStub->async()->StreamLogRecords(&m_streamContext, &m_streamStatus, m_reactor);
    }

    return true;
}

bool ELogGRPCTarget::stopLogTarget() {
    // just delete the stub object
    // TODO: are there any exceptions?
    // TODO: what about shutdown timeouts?
    if (m_clientMode == ELogGRPCClientMode::GRPC_CM_STREAM ||
        m_clientMode == ELogGRPCClientMode::GRPC_CM_ASYNC_CALLBACK_STREAM) {
        // first flush all remaining messages
        flush();
    }
    m_serviceStub.reset();
    if (m_clientMode == ELogGRPCClientMode::GRPC_CM_ASYNC_CALLBACK_STREAM) {
        if (m_reactor != nullptr) {
            m_reactor->waitFlushDone();
            delete m_reactor;
            m_reactor = nullptr;
        }
    }
    return true;
}

uint32_t ELogGRPCTarget::writeLogRecord(const ELogRecord& logRecord) {
    // NOTE: we do not need to format the entire log msg

    // send message to gRPC server
    if (m_clientMode == ELogGRPCClientMode::GRPC_CM_UNARY) {
        // NOTE: receptor must live until message sending, because it holds value strings
        ELogGRPCFieldReceptor receptor;
        elog_grpc::ELogGRPCRecordMsg msg;
        receptor.setLogRecordMsg(&msg);
        fillInParams(logRecord, &receptor);

        grpc::ClientContext context;
        if (m_deadlineTimeoutMillis != 0) {
            setDeadline(context);
        }
        elog_grpc::ELogGRPCStatus status;
        grpc::Status callStatus = m_serviceStub->SendLogRecord(&context, msg, &status);
        if (!callStatus.ok()) {
            ELOG_REPORT_TRACE("Failed to send log record over gRPC: %s",
                              callStatus.error_message().c_str());
            // TODO: what now?
            return 0;
        } else {
            return msg.ByteSizeLong();
        }
    } else if (m_clientMode == ELogGRPCClientMode::GRPC_CM_STREAM) {
        // NOTE: receptor must live until message sending, because it holds value strings
        ELogGRPCFieldReceptor receptor;
        elog_grpc::ELogGRPCRecordMsg msg;
        receptor.setLogRecordMsg(&msg);
        fillInParams(logRecord, &receptor);

        // TODO: this is wrong, we need a context per call, since each has his own distinct deadline
        /*if (m_deadlineTimeoutMillis != 0) {
            setDeadline(m_streamContext);
        }*/
        // write next message in current batch
        if (!m_clientWriter->Write(msg)) {
            ELOG_REPORT_TRACE("Failed to stream log record over gRPC");
            // TODO: what now?
            return 0;
        } else {
            // TODO: fix this: we need to separate submitted/written counters
            return msg.ByteSizeLong();
        }
    } else if (m_clientMode == ELogGRPCClientMode::GRPC_CM_ASYNC) {
        // NOTE: receptor must live until message sending, because it holds value strings
        ELogGRPCFieldReceptor receptor;
        elog_grpc::ELogGRPCRecordMsg msg;
        receptor.setLogRecordMsg(&msg);
        fillInParams(logRecord, &receptor);

        grpc::ClientContext context;
        if (m_deadlineTimeoutMillis != 0) {
            setDeadline(context);
        }
        std::unique_ptr<grpc::ClientAsyncResponseReader<elog_grpc::ELogGRPCStatus>> rpc =
            m_serviceStub->AsyncSendLogRecord(&context, msg, &m_cq);
        elog_grpc::ELogGRPCStatus status;
        grpc::Status callStatus;
        rpc->Finish(&status, &callStatus, (void*)1);
        void* tag = nullptr;
        bool ok = false;
        if (!m_cq.Next(&tag, &ok) || !ok) {
            ELOG_REPORT_TRACE("Failed to get completion queue response in asynchronous mode gRPC");
            // TODO: what now?
            return 0;
        } else if (tag != (void*)1) {
            ELOG_REPORT_TRACE("Unexpected response tag in asynchronous mode gRPC");
            return 0;
        } else if (!callStatus.ok()) {
            ELOG_REPORT_TRACE("Asynchronous mode gRPC call ended with status FAIL: %s",
                              callStatus.error_message().c_str());
            return 0;
        } else {
            return msg.ByteSizeLong();
        }
    } else if (m_clientMode == ELogGRPCClientMode::GRPC_CM_ASYNC_CALLBACK_UNARY) {
        // NOTE: receptor must live until message sending, because it holds value strings
        ELogGRPCFieldReceptor receptor;
        elog_grpc::ELogGRPCRecordMsg msg;
        receptor.setLogRecordMsg(&msg);
        fillInParams(logRecord, &receptor);

        grpc::ClientContext context;
        if (m_deadlineTimeoutMillis != 0) {
            setDeadline(context);
        }
        // should we wait until response arrives before sending the next log record?
        // this is the same question as with async completion queue
        // for this we need to add pipeline mode, both for async and for async unary, but that is
        // what streaming is doing anyway, right?
        // NOTE: we need to wait for the result otherwise the callback will access on-stack local
        // objects that will already be invalid at the callback invocation time, which may cause
        // core dump, or even worse, memory overwrite
        elog_grpc::ELogGRPCStatus status;
        std::mutex responseLock;
        std::condition_variable responseCV;
        bool done = false;
        bool result = false;
        m_serviceStub->async()->SendLogRecord(
            &context, &msg, &status,
            [&responseLock, &responseCV, &done, &result](grpc::Status status) {
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
    } else if (m_clientMode == ELogGRPCClientMode::GRPC_CM_ASYNC_CALLBACK_STREAM) {
        // should we wait until response arrives before sending the next log record?
        // this is the same question as with async completion queue
        // for this we need to add pipeline mode, both for async and for async unary, but that is
        // what streaming is doing anyway, right? so what's the point?
        // NOTE: we need to wait for the result otherwise the callback will access on-stack local
        // objects that will already be invalid at the callback invocation time, which may cause
        // core dump, or even worse, memory overwrite (See solution below)
        return m_reactor->writeLogRecord(logRecord);
        // TODO: we need to generate on-heap call data with members:
        // - dedicated context (since each has a different deadline!)
        // - message being sent (so we keep the receptor in the call data)
        // - protocol status response
        // - grpc status object
        // - perhaps mutex/condition variable for notification
        // the reactor interface does not provide any option for passing user-data/tag so we need to
        // put somewhere the call data, and when flush or log is called we can poll for pending
        // outgoing request status (can use a completion queue instead, so we don't need a mutex and
        // cv, and the call data can be put as a tag in the completion queue) this is actually a
        // poor choice, we already have a custom reactor, so we don't need a queue. instead pass the
        // call data to the reactor. once the call is complete, the reactor can delete the call data
        // (we can use a new reactor each time to avoid locking).
        //
        // in essence we have the following events:
        // - write log record (ext):
        //      if not in batch then start a batch (with AddHold/StartCall)
        //      generate call data and put somewhere
        //      if write is in-flight
        //          push on incoming request queue
        //      else
        //          call StartWrite
        //          raise in-flight flag
        // - on write done (int)
        //      locate call data and release it
        //      check incoming request queue for pending request
        // - flush log record (ext)
        // - on done (int)
    }
}

void ELogGRPCTarget::flushLogTarget() {
    // nothing here now
    if (m_clientMode == ELogGRPCClientMode::GRPC_CM_STREAM) {
        m_clientWriter->WritesDone();
        grpc::Status callStatus = m_clientWriter->Finish();
        if (!callStatus.ok()) {
            ELOG_REPORT_TRACE("Failed to finish log record stream sending over gRPC: %s",
                              callStatus.error_message().c_str());
        }
        m_clientWriter.reset();

        // regenerate client writer for next messages
        new (&m_streamContext) grpc::ClientContext();
        m_clientWriter = m_serviceStub->StreamLogRecords(&m_streamContext, &m_streamStatus);
    } else if (m_clientMode == ELogGRPCClientMode::GRPC_CM_ASYNC_CALLBACK_STREAM) {
        m_reactor->flush();
        // we must for flush to finish properly, then regenerate reactor
        m_reactor->waitFlushDone();

        // regenerate reactor for next messages
        delete m_reactor;
        m_reactor = new ELogReactor(m_serviceStub.get(), getRpcFormatter(), m_maxInflightCalls);
        m_streamContext.~ClientContext();
        new (&m_streamContext) grpc::ClientContext();
        m_serviceStub->async()->StreamLogRecords(&m_streamContext, &m_streamStatus, m_reactor);
        ELOG_REPORT_TRACE("Reactor regenerated at %p", m_reactor);
    }
}

}  // namespace elog

#endif  // ELOG_ENABLE_MYSQL_DB_CONNECTOR