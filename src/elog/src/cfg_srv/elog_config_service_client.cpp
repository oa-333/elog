#include "cfg_srv/elog_config_service_client.h"

#ifdef ELOG_ENABLE_CONFIG_SERVICE

#include <msg/msg_frame_writer.h>

#include "elog_report.h"

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogConfigServiceClient)

bool ELogConfigServiceClient::initialize(const char* host, int port,
                                         uint32_t maxConcurrentRequests /* = 1 */) {
    m_tcpClient.configure(host, port, 5000);
    commutil::ErrorCode rc = m_msgClient.initialize(&m_tcpClient, maxConcurrentRequests);
    if (rc != commutil::ErrorCode::E_OK) {
        ELOG_REPORT_ERROR("Failed to initialize message client: %s",
                          commutil::errorCodeToString(rc));
        return false;
    }
    return true;
}

bool ELogConfigServiceClient::terminate() {
    commutil::ErrorCode rc = m_msgClient.terminate();
    if (rc != commutil::ErrorCode::E_OK) {
        ELOG_REPORT_ERROR("Failed to terminate message client: %s",
                          commutil::errorCodeToString(rc));
        return false;
    }
    return true;
}

bool ELogConfigServiceClient::start() {
    commutil::ErrorCode rc = m_msgClient.start();
    if (rc != commutil::ErrorCode::E_OK) {
        ELOG_REPORT_ERROR("Failed to start message client: %s", commutil::errorCodeToString(rc));
        (void)m_msgClient.terminate();
        return false;
    }
    return true;
}

bool ELogConfigServiceClient::stop() {
    commutil::ErrorCode rc = m_msgClient.stop();
    if (rc != commutil::ErrorCode::E_OK) {
        ELOG_REPORT_ERROR("Failed to stop message client: %s", commutil::errorCodeToString(rc));
        return false;
    }
    return true;
}

bool ELogConfigServiceClient::waitReady() {
    int status = 0;
    commutil::ErrorCode rc = m_msgClient.waitConnect(&status);
    if (rc != commutil::ErrorCode::E_OK) {
        ELOG_REPORT_ERROR("Failed waiting for message client to connect: %s",
                          commutil::errorCodeToString(rc));
        return false;
    }
    if (status != 0) {
        ELOG_REPORT_ERROR("Message client connect attempt failed with status code: %d", status);
        return false;
    }
    return true;
}

bool ELogConfigServiceClient::queryLogLevels(const char* includeRegEx, const char* excludeRegEx,
                                             std::unordered_map<std::string, ELogLevel>& logLevels,
                                             ELogLevel& reportLevel) {
    elog_grpc::ELogConfigLevelQueryMsg configLevelQueryMsg;
    if (includeRegEx != nullptr && *includeRegEx != 0) {
        configLevelQueryMsg.set_includeregex(includeRegEx);
    } else {
        configLevelQueryMsg.set_includeregex(".*");
    }
    if (excludeRegEx != nullptr && *excludeRegEx != 0) {
        configLevelQueryMsg.set_excluderegex(excludeRegEx);
    }
    commutil::Msg* request = prepareRequest(ELOG_CONFIG_LEVEL_QUERY_MSG_ID, configLevelQueryMsg);
    if (request == nullptr) {
        return false;
    }

    commutil::ErrorCode rc = m_msgClient.transactMsg(
        request, COMMUTIL_MSG_INFINITE_TIMEOUT,
        [&logLevels, &reportLevel](commutil::Msg* response) -> commutil::ErrorCode {
            if (response->getHeader().getMsgId() != ELOG_CONFIG_LEVEL_REPORT_MSG_ID) {
                ELOG_REPORT_ERROR(
                    "Invalid response type %u, expecting ELOG_CONFIG_LEVEL_MSG_ID (%u)",
                    (unsigned)response->getHeader().getMsgId(),
                    (unsigned)ELOG_CONFIG_LEVEL_REPORT_MSG_ID);
                return commutil::ErrorCode::E_PROTOCOL_ERROR;
            }
            elog_grpc::ELogConfigLevelReportMsg configLevelReportMsg;
            if (!configLevelReportMsg.ParseFromArray(response->getPayload(),
                                                     (int)response->getPayloadSizeBytes())) {
                ELOG_REPORT_ERROR("Failed to deserialize log level message (protobuf)");
                return commutil::ErrorCode::E_DATA_CORRUPT;
            }
            for (const auto& pair : configLevelReportMsg.loglevels()) {
                logLevels.insert(std::unordered_map<std::string, ELogLevel>::value_type(
                    pair.first, (ELogLevel)pair.second));
            }
            reportLevel = (ELogLevel)configLevelReportMsg.reportlevel();
            return commutil::ErrorCode::E_OK;
        });

    if (rc != commutil::ErrorCode::E_OK) {
        ELOG_REPORT_ERROR("Failed to query log levels: %s", commutil::errorCodeToString(rc));
    }
    return (rc == commutil::ErrorCode::E_OK);
}

bool ELogConfigServiceClient::updateLogLevels(
    const std::unordered_map<std::string, std::pair<ELogLevel, ELogPropagateMode>>& logLevels,
    int& status, std::string& errorMsg) {
    elog_grpc::ELogConfigLevelUpdateMsg configLevelUpdateMsg;
    for (const auto& pair : logLevels) {
        elog_grpc::ELogConfigLevelUpdateMsg_ELogLevelConfig msg;
        msg.set_loglevel((elog_grpc::ELogLevel)pair.second.first);
        msg.set_propagatemode((elog_grpc::ELogPropagateMode)pair.second.second);
        (*configLevelUpdateMsg.mutable_loglevels())[pair.first] = msg;
    }

    commutil::Msg* request = prepareRequest(ELOG_CONFIG_LEVEL_UPDATE_MSG_ID, configLevelUpdateMsg);
    if (request == nullptr) {
        return false;
    }

    return transactReply(request, status, errorMsg);
}

bool ELogConfigServiceClient::updateReportLevel(ELogLevel reportLevel, int& status,
                                                std::string& errorMsg) {
    elog_grpc::ELogConfigLevelUpdateMsg configLevelUpdateMsg;
    configLevelUpdateMsg.set_reportlevel((elog_grpc::ELogLevel)reportLevel);

    commutil::Msg* request = prepareRequest(ELOG_CONFIG_LEVEL_UPDATE_MSG_ID, configLevelUpdateMsg);
    if (request == nullptr) {
        return false;
    }

    return transactReply(request, status, errorMsg);
}

bool ELogConfigServiceClient::updateLogReportLevels(
    const std::unordered_map<std::string, std::pair<ELogLevel, ELogPropagateMode>>& logLevels,
    ELogLevel reportLevel, int& status, std::string& errorMsg) {
    elog_grpc::ELogConfigLevelUpdateMsg configLevelUpdateMsg;
    for (const auto& pair : logLevels) {
        elog_grpc::ELogConfigLevelUpdateMsg_ELogLevelConfig msg;
        msg.set_loglevel((elog_grpc::ELogLevel)pair.second.first);
        msg.set_propagatemode((elog_grpc::ELogPropagateMode)pair.second.second);
        (*configLevelUpdateMsg.mutable_loglevels())[pair.first] = msg;
    }
    configLevelUpdateMsg.set_reportlevel((elog_grpc::ELogLevel)reportLevel);

    commutil::Msg* request = prepareRequest(ELOG_CONFIG_LEVEL_UPDATE_MSG_ID, configLevelUpdateMsg);
    if (request == nullptr) {
        return false;
    }

    return transactReply(request, status, errorMsg);
}

commutil::Msg* ELogConfigServiceClient::prepareRequest(uint16_t msgId,
                                                       const ::google::protobuf::Message& msg) {
    size_t size = msg.ByteSizeLong();
    ELogMsgBuffer msgBuffer(size);
    if (!msg.SerializeToArray(&msgBuffer[0], (int)size)) {
        ELOG_REPORT_ERROR("Message serialization error");
        return nullptr;
    }

    commutil::Msg* request = nullptr;
    commutil::ErrorCode rc =
        commutil::MsgFrameWriter::prepareMsgFrame(&request, msgId, &msgBuffer[0], msgBuffer.size());
    if (rc != commutil::ErrorCode::E_OK) {
        ELOG_REPORT_ERROR("Failed to prepare message frame: %s", commutil::errorCodeToString(rc));
        return nullptr;
    }
    return request;
}

bool ELogConfigServiceClient::transactReply(commutil::Msg* request, int& status,
                                            std::string& errorMsg) {
    commutil::ErrorCode rc = m_msgClient.transactMsg(
        request, COMMUTIL_MSG_INFINITE_TIMEOUT,
        [&status, &errorMsg](commutil::Msg* response) -> commutil::ErrorCode {
            if (response->getHeader().getMsgId() != ELOG_CONFIG_LEVEL_REPLY_MSG_ID) {
                ELOG_REPORT_ERROR(
                    "Invalid response type %u, expecting ELOG_CONFIG_LEVEL_REPLY_MSG_ID (%u)",
                    (unsigned)response->getHeader().getMsgId(),
                    (unsigned)ELOG_CONFIG_LEVEL_REPLY_MSG_ID);
                return commutil::ErrorCode::E_PROTOCOL_ERROR;
            }
            elog_grpc::ELogConfigLevelReplyMsg configLevelReplyMsg;
            if (!configLevelReplyMsg.ParseFromArray(response->getPayload(),
                                                    (int)response->getPayloadSizeBytes())) {
                ELOG_REPORT_ERROR("Failed to deserialize log level reply message (protobuf)");
                return commutil::ErrorCode::E_DATA_CORRUPT;
            }
            status = configLevelReplyMsg.status();
            errorMsg = configLevelReplyMsg.errormsg();
            return commutil::ErrorCode::E_OK;
        });

    if (rc != commutil::ErrorCode::E_OK) {
        ELOG_REPORT_ERROR("Failed to transact message with remote configuration service: %s",
                          commutil::errorCodeToString(rc));
    }
    return (rc == commutil::ErrorCode::E_OK);
}

}  // namespace elog

#endif  // ELOG_ENABLE_CONFIG_SERVICE