#include "msg/elog_msg_target.h"

#ifdef ELOG_ENABLE_MSG

#include <msg/msg_buffer_array_writer.h>

#include "elog_report.h"

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogMsgTarget)

ELogMsgTarget::~ELogMsgTarget() {
    if (m_dataClient != nullptr) {
        delete m_dataClient;
        m_dataClient = nullptr;
    }
    if (m_binaryFormatProvider != nullptr) {
        delete m_binaryFormatProvider;
        m_binaryFormatProvider = nullptr;
    }
}

void ELogMsgTarget::onSendMsgStats(uint32_t msgSizeBytes, uint32_t compressedMsgSizeBytes,
                                   int status) {
    if (m_enableStats && m_msgStats != nullptr) {
        m_msgStats->updateSendStats(msgSizeBytes, compressedMsgSizeBytes, status);
    }
}

void ELogMsgTarget::onRecvMsgStats(uint32_t msgSizeBytes, uint32_t compressedMsgSizeBytes) {
    // NOTE: since in ELog protocol the status response is not compressed, we ignore this
    // notification altogether and report statistics during handling of incoming response
}

uint64_t ELogMsgTarget::getProcessedMsgCount() {
    if (!m_enableStats || m_msgStats == nullptr) {
        return ELOG_INVALID_MSG_COUNT;
    }
    return m_msgStats->getProcessedMsgCount().getSum();
}

commutil::ErrorCode ELogMsgTarget::handleMsg(const commutil::ConnectionDetails& connectionDetails,
                                             const commutil::MsgHeader& msgHeader,
                                             const char* msgBuffer, uint32_t bufferSize,
                                             bool lastInBatch, uint32_t batchSize) {
    // check message id expected according to protocol
    uint32_t msgId = msgHeader.getMsgId();
    if (msgId != ELOG_STATUS_MSG_ID) {
        ELOG_REPORT_ERROR("Unexpected response message identifier: %u", msgId);
        return commutil::ErrorCode::E_PROTOCOL_ERROR;
    }

    // deserialize status message
    ELogStatusMsg statusMsg;
    if (!m_binaryFormatProvider->logStatusFromBuffer(statusMsg, msgBuffer, bufferSize)) {
        ELOG_REPORT_ERROR("Failed to deserialize status response message");
        return commutil::ErrorCode::E_DATA_CORRUPT;
    }

    // check status
    if (statusMsg.getStatus() != 0) {
        ELOG_REPORT_ERROR("Received status %d from server", statusMsg.getStatus());
        if (m_enableStats && m_msgStats != nullptr) {
            m_msgStats->incrementRecvFailCount();
        }
        return commutil::ErrorCode::E_SERVER_ERROR;
    }

    // update statistics
    m_msgStats->updateRecvStats(bufferSize, statusMsg.getRecordsProcessed());
    return commutil::ErrorCode::E_OK;
}

void ELogMsgTarget::handleMsgError(const commutil::ConnectionDetails& connectionDetails,
                                   const commutil::MsgHeader& msgHeader, int status) {
    if (m_enableStats && m_msgStats != nullptr) {
        m_msgStats->incrementRecvFailCount();
    }
}

bool ELogMsgTarget::startLogTarget() {
    commutil::ErrorCode rc = m_msgClient.initialize(m_dataClient, m_maxConcurrentRequests);
    if (rc != commutil::ErrorCode::E_OK) {
        ELOG_REPORT_ERROR("Failed to initialize message client: %s",
                          commutil::errorCodeToString(rc));
        return false;
    }
    rc = m_msgSender.initialize(&m_msgClient, m_msgConfig, this, m_enableStats ? this : nullptr);
    if (rc != commutil::ErrorCode::E_OK) {
        ELOG_REPORT_ERROR("Failed to initialize message sender: %s",
                          commutil::errorCodeToString(rc));
        (void)m_msgClient.terminate();
        return false;
    }
    m_msgClient.setName(getName());
    rc = m_msgSender.start();
    if (rc != commutil::ErrorCode::E_OK) {
        ELOG_REPORT_ERROR("Failed to start message sender: %s", commutil::errorCodeToString(rc));
        (void)m_msgSender.terminate();
        (void)m_msgClient.terminate();
        return false;
    }
    return true;
}

bool ELogMsgTarget::stopLogTarget() {
    commutil::ErrorCode rc = m_msgSender.stop();
    if (rc != commutil::ErrorCode::E_OK) {
        ELOG_REPORT_ERROR("Failed to stop message sender: %s", commutil::errorCodeToString(rc));
        return false;
    }
    rc = m_msgSender.terminate();
    if (rc != commutil::ErrorCode::E_OK) {
        ELOG_REPORT_ERROR("Failed to terminate message sender: %s",
                          commutil::errorCodeToString(rc));
        return false;
    }
    rc = m_msgClient.terminate();
    if (rc != commutil::ErrorCode::E_OK) {
        ELOG_REPORT_ERROR("Failed to terminate message client: %s",
                          commutil::errorCodeToString(rc));
        return false;
    }
    return true;
}

uint32_t ELogMsgTarget::writeLogRecord(const ELogRecord& logRecord) {
    ELOG_REPORT_DEBUG("Preapring log message");

    // use the binary format provider to convert the log record into a byte array
    // and put it in the data buffer array
    commutil::MsgBuffer& msgBuffer = m_msgBufferArray.emplace_back();
    if (!m_binaryFormatProvider->logRecordToBuffer(logRecord, getLogFormatter(), msgBuffer)) {
        ELOG_REPORT_ERROR("Failed to serialize log record into buffer");
        return 0;
    }
    return (uint32_t)msgBuffer.size();
}

bool ELogMsgTarget::flushLogTarget() {
    // send message
    bool res = true;
    if (!m_msgBufferArray.empty()) {
        if (m_syncMode) {
            commutil::ErrorCode rc = m_msgSender.transactMsgBatch(
                ELOG_RECORD_MSG_ID, m_msgBufferArray, m_compress, COMMUTIL_MSG_FLAG_BATCH,
                m_msgConfig.m_sendTimeoutMillis);
            if (rc != commutil::ErrorCode::E_OK) {
                ELOG_REPORT_ERROR("Failed to transact message: %s",
                                  commutil::errorCodeToString(rc));
                res = false;
            }
        } else {
            commutil::ErrorCode rc = m_msgSender.sendMsgBatch(ELOG_RECORD_MSG_ID, m_msgBufferArray,
                                                              m_compress, COMMUTIL_MSG_FLAG_BATCH);
            if (rc != commutil::ErrorCode::E_OK) {
                ELOG_REPORT_ERROR("Failed to transact message: %s",
                                  commutil::errorCodeToString(rc));
                res = false;
            }
        }

        // clear the buffer array for next round
        m_msgBufferArray.clear();
    }

    // NOTE: if resend needs to take place, then the body has already been copied to the backlog
    return res;
}

ELogStats* ELogMsgTarget::createStats() {
    m_msgStats = new (std::nothrow) ELogMsgStats();
    return m_msgStats;
}

}  // namespace elog

#endif  // ELOG_ENABLE_MSG