#include "msg/elog_msg_server.h"

#ifdef ELOG_ENABLE_MSG

#include "elog_report.h"
#include "msg/elog_binary_format_provider.h"

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogMsgServer)

commutil::ErrorCode ELogMsgServer::handleMsg(const commutil::ConnectionDetails& connDetails,
                                             const commutil::MsgHeader& msgHeader,
                                             const char* buffer, uint32_t length, bool lastInBatch,
                                             uint32_t batchSize) {
    // get the session object
    commutil::MsgServer::Session* session = nullptr;
    commutil::ErrorCode rc = getSession(connDetails, &session);
    if (rc != commutil::ErrorCode::E_OK) {
        ELOG_REPORT_ERROR("Rejecting log record message, invalid session: %s",
                          commutil::errorCodeToString(rc));
        return rc;
    }

    // first check for duplicates and reject if already handled
    ELogSession* elogSession = (ELogSession*)session;
    if (elogSession->m_rollingBitset.contains(msgHeader.getRequestId())) {
        ELOG_REPORT_TRACE("Rejecting duplicate message with request id %" PRIu64
                          ", request already handled",
                          msgHeader.getRequestId());
        return commutil::ErrorCode::E_ALREADY_EXISTS;
    }

    // deserialize message and handle it
    elog::ELogProtobufBinaryFormatProvider binaryFormatProvider(m_byteOrder);
    elog_grpc::ELogRecordMsg recordMsg;
    if (!recordMsg.ParseFromArray(buffer, (int)length)) {
        ELOG_REPORT_ERROR("Failed to deserialize log record message (protobuf)");
        handleMsgError(connDetails, msgHeader, (int)commutil::ErrorCode::E_PROTOCOL_ERROR);
        return commutil::ErrorCode::E_DATA_CORRUPT;
    }

    // handle record in batch and remember first error in batch
    int status = handleLogRecordMsg(&recordMsg);
    if (status != 0 && elogSession->m_status == 0) {
        elogSession->m_status = status;
    }

    // send status to client only when batch is done
    if (lastInBatch) {
        // report any error seen in batch
        sendStatus(connDetails, msgHeader, elogSession->m_status, batchSize);

        // only if entire batch was ok we can mark the batch as handled
        if (elogSession->m_status == 0) {
            elogSession->m_rollingBitset.insert(msgHeader.getRequestId());
        }

        // reset status for next batch
        elogSession->m_status = 0;
    }

    return commutil::ErrorCode::E_OK;
}

void ELogMsgServer::handleMsgError(const commutil::ConnectionDetails& connDetails,
                                   const commutil::MsgHeader& msgHeader, int status) {
    sendStatus(connDetails, msgHeader, status, 0);
}

void ELogMsgServer::sendStatus(const commutil::ConnectionDetails& connectionDetails,
                               const commutil::MsgHeader& msgHeader, int status,
                               uint64_t recordsProcessed) {
    elog::ELogProtobufBinaryFormatProvider bfp(m_byteOrder);
    elog::ELogStatusMsg statusMsg;
    statusMsg.setStatus(status);
    statusMsg.setRecordsProcessed(recordsProcessed);
    ELogMsgBuffer msgBuffer;
    if (!bfp.logStatusToBuffer(statusMsg, msgBuffer)) {
        ELOG_REPORT_ERROR("Status message serialization error");
    } else {
        commutil::Msg* response =
            commutil::allocMsg(ELOG_STATUS_MSG_ID, 0, msgHeader.getRequestId(),
                               msgHeader.getRequestIndex(), (uint32_t)msgBuffer.size());
        if (response == nullptr) {
            ELOG_REPORT_ERROR("Failed to allocate status response message");
        } else {
            memcpy(response->modifyPayload(), &msgBuffer[0], msgBuffer.size());
            commutil::ErrorCode rc = replyMsg(connectionDetails, response);
            if (rc != commutil::ErrorCode::E_OK)
                ELOG_REPORT_ERROR("Failed to send status response to client: %s",
                                  commutil::errorCodeToString(rc));
        }
        commutil::freeMsg(response);
    }
}

commutil::MsgServer::Session* ELogMsgServer::createSession(
    uint64_t sessionId, const commutil::ConnectionDetails& connectionDetails) {
    return new (std::nothrow) ELogSession(sessionId, connectionDetails, m_maxDelayMsgSpan);
}

}  // namespace elog

#endif  // ELOG_ENABLE_MSG