#include "cfg_srv/elog_config_service.h"

#ifdef ELOG_ENABLE_CONFIG_SERVICE

#include <cassert>

#include "elog_api.h"
#include "elog_internal.h"
#include "elog_report.h"
#include "msg/elog_msg.h"

// we do not expect a thundering herd of connecting clients
#define ELOG_CONFIG_SERVICE_BACKLOG 1

// very limited amount of concurrent clients is expected
#define ELOG_CONFIG_SERVICE_MAX_CONNECTIONS 5

// one I/O thread is enough
#define ELOG_CONFIG_SERVICE_IO_CONCURRENCY 1

// one message processing thread is enough
#define ELOG_CONFIG_SERVICE_MSG_CONCURRENCY 1

// currently not in use
#define ELOG_CONFIG_BUFFER_SIZE 4096

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogConfigService)

ELogConfigService* ELogConfigService::sInstance = nullptr;

bool ELogConfigService::createInstance() {
    if (sInstance != nullptr) {
        ELOG_REPORT_ERROR("Cannot create remote configuration instance, already created");
        return false;
    }
    sInstance = new (std::nothrow) ELogConfigService();
    if (sInstance == nullptr) {
        ELOG_REPORT_ERROR("Failed to create remote configuration instance, out of memory");
        return false;
    }
    return true;
}

bool ELogConfigService::destroyInstance() {
    if (sInstance == nullptr) {
        ELOG_REPORT_ERROR("Cannot destroy remote configuration instance, already destroyed");
        return false;
    }
    delete sInstance;
    sInstance = nullptr;
    return true;
}

ELogConfigService* ELogConfigService::getInstance() {
    assert(sInstance != nullptr);
    return sInstance;
}

commutil::ErrorCode ELogConfigService::initialize(
    const char* iface, int port, ELogConfigServicePublisher* publisher /* = nullptr */) {
    // delegate to message server
    m_tcpServer.configure(iface, port, ELOG_CONFIG_SERVICE_BACKLOG,
                          ELOG_CONFIG_SERVICE_IO_CONCURRENCY);
    commutil::ErrorCode rc =
        m_msgServer.initialize(&m_tcpServer, ELOG_CONFIG_SERVICE_MAX_CONNECTIONS,
                               ELOG_CONFIG_SERVICE_MSG_CONCURRENCY, ELOG_CONFIG_BUFFER_SIZE, this);
    if (rc != commutil::ErrorCode::E_OK) {
        return rc;
    }
    if (publisher != nullptr) {
        m_publisher = publisher;
    }
    m_msgServer.setName("ELogConfigService");
    return rc;
}

void ELogConfigService::setListenAddress(const char* iface, int port) {
    m_tcpServer.setInterface(iface);
    m_tcpServer.setPort(port);
}

commutil::ErrorCode ELogConfigService::terminate() {
    // delegate to message server
    return m_msgServer.terminate();
}

commutil::ErrorCode ELogConfigService::start() {
    // delegate to message server
    commutil::ErrorCode rc = m_msgServer.start();
    if (rc == commutil::ErrorCode::E_OK && m_publisher != nullptr) {
        ELOG_REPORT_TRACE("Starting configuration service on %s:%d", m_tcpServer.getRealInterface(),
                          m_tcpServer.getRealPort());
        m_publisher->onConfigServiceStart(m_tcpServer.getRealInterface(),
                                          m_tcpServer.getRealPort());
    }
    return rc;
}

commutil::ErrorCode ELogConfigService::stop() {
    // delegate to message server
    commutil::ErrorCode rc = m_msgServer.stop();
    if (rc == commutil::ErrorCode::E_OK && m_publisher != nullptr) {
        m_publisher->onConfigServiceStop(m_tcpServer.getRealInterface(), m_tcpServer.getRealPort());
    }
    return rc;
}

commutil::ErrorCode ELogConfigService::restart() {
    commutil::ErrorCode rc = stop();
    if (rc != commutil::ErrorCode::E_OK && rc != commutil::ErrorCode::E_INVALID_STATE) {
        ELOG_REPORT_ERROR("Failed to restart configuration service, call to stop() failed: %s",
                          commutil::errorCodeToString(rc));
        return rc;
    }

    rc = start();
    if (rc != commutil::ErrorCode::E_OK) {
        ELOG_REPORT_ERROR("Failed to restart configuration service, call to start() failed: %s",
                          commutil::errorCodeToString(rc));
        return rc;
    }
    return commutil::ErrorCode::E_OK;
}

commutil::ErrorCode ELogConfigService::handleMsg(const commutil::ConnectionDetails& connDetails,
                                                 const commutil::MsgHeader& msgHeader,
                                                 const char* buffer, uint32_t length,
                                                 bool lastInBatch, uint32_t batchSize) {
    // get the session object
    commutil::MsgSession* session = nullptr;
    commutil::ErrorCode rc = m_msgServer.getSession(connDetails, &session);
    if (rc != commutil::ErrorCode::E_OK) {
        ELOG_REPORT_ERROR("Rejecting log record message, invalid session: %s",
                          commutil::errorCodeToString(rc));
        return rc;
    }

    switch (msgHeader.getMsgId()) {
        case ELOG_CONFIG_LEVEL_QUERY_MSG_ID:
            rc = handleConfigLevelQueryMsg(connDetails, msgHeader, buffer, length, lastInBatch,
                                           batchSize);
            break;

        case ELOG_CONFIG_LEVEL_UPDATE_MSG_ID:
            rc = handleConfigLevelUpdateMsg(connDetails, msgHeader, buffer, length, lastInBatch,
                                            batchSize);
            break;

        default: {
            ELOG_REPORT_ERROR("Invalid configuration service message id %u",
                              (unsigned)msgHeader.getMsgId());
            std::string errorMsg = std::string("Invalid configuration service message id ") +
                                   std::to_string(msgHeader.getMsgId());
            sendReplyError(connDetails, msgHeader, (int)commutil::ErrorCode::E_PROTOCOL_ERROR,
                           errorMsg.c_str());
            rc = commutil::ErrorCode::E_PROTOCOL_ERROR;
            break;
        }
    }

    return rc;
}

void ELogConfigService::handleMsgError(const commutil::ConnectionDetails& connDetails,
                                       const commutil::MsgHeader& msgHeader, int status) {
    sendReplyError(connDetails, msgHeader, status, "Failed to process incoming message");
}

commutil::ErrorCode ELogConfigService::handleConfigLevelQueryMsg(
    const commutil::ConnectionDetails& connectionDetails, const commutil::MsgHeader& msgHeader,
    const char* msgBuffer, uint32_t bufferSize, bool lastInBatch, uint32_t batchSize) {
    // deserialize message from payload
    elog_grpc::ELogConfigLevelQueryMsg configLevelQueryMsg;
    if (!configLevelQueryMsg.ParseFromArray(msgBuffer, (int)bufferSize)) {
        ELOG_REPORT_ERROR("Failed to deserialize configuration query message");
        handleMsgError(connectionDetails, msgHeader, (int)commutil::ErrorCode::E_PROTOCOL_ERROR);
        return commutil::ErrorCode::E_DATA_CORRUPT;
    }

    std::string includeRegEx = ".*";
    std::string excludeRegEx = "";
    if (configLevelQueryMsg.has_includeregex()) {
        includeRegEx = configLevelQueryMsg.includeregex();
    }
    if (configLevelQueryMsg.has_excluderegex()) {
        excludeRegEx = configLevelQueryMsg.excluderegex();
    }

    // TODO: change namespace elog_grpc --> elog_proto
    // TODO: fix all protobuf member names to use underscore

    // get all levels of all log sources and put in response
    elog_grpc::ELogConfigLevelReportMsg configLevelReportMsg;
    forEachLogSource(includeRegEx.c_str(), excludeRegEx.c_str(),
                     [&configLevelReportMsg](ELogSource* logSource) -> void {
                         configLevelReportMsg.mutable_loglevels()->insert(
                             std::make_pair(std::string(logSource->getQualifiedName()),
                                            (elog_grpc::ELogLevel)logSource->getLogLevel()));
                     });
    configLevelReportMsg.set_reportlevel((elog_grpc::ELogLevel)getReportLevel());

    // serialize response and send to client
    return sendResponse(connectionDetails, msgHeader, ELOG_CONFIG_LEVEL_REPORT_MSG_ID,
                        configLevelReportMsg);
}

commutil::ErrorCode ELogConfigService::handleConfigLevelUpdateMsg(
    const commutil::ConnectionDetails& connectionDetails, const commutil::MsgHeader& msgHeader,
    const char* msgBuffer, uint32_t bufferSize, bool lastInBatch, uint32_t batchSize) {
    // deserialize message from payload
    elog_grpc::ELogConfigLevelUpdateMsg configLevelUpdateMsg;
    if (!configLevelUpdateMsg.ParseFromArray(msgBuffer, (int)bufferSize)) {
        ELOG_REPORT_ERROR("Failed to deserialize configuration level update message");
        handleMsgError(connectionDetails, msgHeader, (int)commutil::ErrorCode::E_PROTOCOL_ERROR);
        return commutil::ErrorCode::E_DATA_CORRUPT;
    }

    // configure all levels of all log sources and put in response
    // accumulate all errors (log source not found, etc.) into string
    elog_grpc::ELogConfigLevelReplyMsg configLevelReplyMsg;
    configLevelReplyMsg.set_status(0);
    configLevelReplyMsg.set_errormsg("No error");
    bool hasError = false;
    for (auto& entry : configLevelUpdateMsg.loglevels()) {
        // first attempt as regular expression
        std::vector<ELogSource*> logSources;
        getLogSourcesEx(entry.first.c_str(), "", logSources);
        if (!logSources.empty()) {
            for (ELogSource* logSource : logSources) {
                logSource->setLogLevel((ELogLevel)entry.second.loglevel(),
                                       (ELogPropagateMode)entry.second.propagatemode());
            }
        } else {
            ELogSource* logSource = getLogSource(entry.first.c_str());
            if (logSource == nullptr) {
                // report error
                configLevelReplyMsg.set_status((int)commutil::ErrorCode::E_NOT_FOUND);
                std::string errorMsg = std::string("log source(s) ") + entry.first + " not found";
                if (!hasError) {
                    configLevelReplyMsg.set_errormsg(errorMsg);
                    hasError = true;
                } else {
                    configLevelReplyMsg.mutable_errormsg()->append(std::string("\n") + errorMsg);
                }
            } else {
                // set the log level with configured propagation
                logSource->setLogLevel((ELogLevel)entry.second.loglevel(),
                                       (ELogPropagateMode)entry.second.propagatemode());
            }
        }
    }
    if (configLevelUpdateMsg.has_reportlevel()) {
        setReportLevel((ELogLevel)configLevelUpdateMsg.reportlevel());
    }

    // serialize response and send to client
    return sendResponse(connectionDetails, msgHeader, ELOG_CONFIG_LEVEL_REPLY_MSG_ID,
                        configLevelReplyMsg);
}

commutil::ErrorCode ELogConfigService::sendResponse(
    const commutil::ConnectionDetails& connectionDetails, const commutil::MsgHeader& msgHeader,
    uint16_t msgId, const ::google::protobuf::Message& msg) {
    // serialize message to buffer
    size_t size = msg.ByteSizeLong();
    ELogMsgBuffer msgBuffer(size);
    if (!msg.SerializeToArray(&msgBuffer[0], (int)size)) {
        ELOG_REPORT_ERROR("Message serialization error");
        return commutil::ErrorCode::E_PROTOCOL_ERROR;
    }

    // allocate response frame
    commutil::Msg* response =
        commutil::allocMsg(msgId, 0, msgHeader.getRequestId(), msgHeader.getRequestIndex(),
                           (uint32_t)msgBuffer.size());
    if (response == nullptr) {
        ELOG_REPORT_ERROR("Failed to allocate response message");
        return commutil::ErrorCode::E_NOMEM;
    }

    // serialize payload into frame
    memcpy(response->modifyPayload(), &msgBuffer[0], msgBuffer.size());
    commutil::ErrorCode rc = m_msgServer.replyMsg(connectionDetails, response);
    if (rc != commutil::ErrorCode::E_OK) {
        ELOG_REPORT_ERROR("Failed to send response to client: %s", commutil::errorCodeToString(rc));
    }

    // cleanup and return
    commutil::freeMsg(response);
    return rc;
}

void ELogConfigService::sendReplyError(const commutil::ConnectionDetails& connectionDetails,
                                       const commutil::MsgHeader& msgHeader, int status,
                                       const char* errorMsg) {
    elog_grpc::ELogConfigLevelReplyMsg configLevelReplyMsg;
    configLevelReplyMsg.set_status(status);
    configLevelReplyMsg.set_errormsg(errorMsg);

    // serialize response and send to client
    commutil::ErrorCode rc = sendResponse(connectionDetails, msgHeader,
                                          ELOG_CONFIG_LEVEL_REPLY_MSG_ID, configLevelReplyMsg);
    if (rc != commutil::ErrorCode::E_OK) {
        ELOG_REPORT_ERROR("Failed to send error response to client: %s",
                          commutil::errorCodeToString(rc));
    }
}

}  // namespace elog

#endif  // ELOG_ENABLE_CONFIG_SERVICE