#ifndef __ELOG_CONFIG_SERVICE_CLIENT_H__
#define __ELOG_CONFIG_SERVICE_CLIENT_H__

#ifdef ELOG_ENABLE_CONFIG_SERVICE

#include <msg/msg.h>
#include <msg/msg_client.h>
#include <msg/msg_config.h>
#include <msg/msg_def.h>
#include <transport/tcp_client.h>

#include "elog_proto.h"
#include "elog_target.h"
#include "msg/elog_msg.h"

namespace elog {

/** @brief Abstract parent class for messaging log targets. */
class ELOG_API ELogConfigServiceClient {
public:
    ELogConfigServiceClient() {}
    ELogConfigServiceClient(const ELogConfigServiceClient&) = delete;
    ELogConfigServiceClient(ELogConfigServiceClient&&) = delete;
    ELogConfigServiceClient& operator=(const ELogConfigServiceClient&) = delete;
    ~ELogConfigServiceClient() {}

    /**
     * @brief Initializes the configuration service client.
     * @param host The remote configuration service host.
     * @param port The remote configuration service port.
     * @param maxConcurrentRequests the maximum number of concurrent pending requests.
     * @return The operation's result.
     */
    bool initialize(const char* host, int port, uint32_t maxConcurrentRequests = 1);

    /** @brief Terminates the configuration service client. */
    bool terminate();

    /** @brief Starts the configuration service client. */
    bool start();

    /** @brief Stops the configuration service client. */
    bool stop();

    /** @brief Waits for the message client to connect to the server. */
    bool waitReady();

    /** @brief Queries for log levels. */

    /**
     * @brief Queries for log levels of the target process.
     * @param includeRegEx Inclusion regular expression for filtering log sources by name.
     * @param excludeRegEx Exclusion regular expression for filtering log sources by name.
     * @param logLevels The resulting log levels by log source name.
     * @param reportLevel The ELog framework's internal reporting log level.
     * @return The operation's result.
     */
    bool queryLogLevels(const char* includeRegEx, const char* excludeRegEx,
                        std::unordered_map<std::string, ELogLevel>& logLevels,
                        ELogLevel& reportLevel);

    /** @brief Sets the log levels. */
    bool updateLogLevels(
        const std::unordered_map<std::string, std::pair<ELogLevel, ELogPropagateMode>>& logLevels,
        int& status, std::string& errorMsg);

    /** @brief Sets the ELog's internal report log level. */
    bool updateReportLevel(ELogLevel reportLevel, int& status, std::string& errorMsg);

    /** @brief Sets the log levels and the report level. */
    bool updateLogReportLevels(
        const std::unordered_map<std::string, std::pair<ELogLevel, ELogPropagateMode>>& logLevels,
        ELogLevel reportLevel, int& status, std::string& errorMsg);

private:
    commutil::TcpClient m_tcpClient;
    commutil::MsgClient m_msgClient;

    commutil::Msg* prepareRequest(uint16_t msgId, const ::google::protobuf::Message& msg);

    bool transactReply(commutil::Msg* request, int& status, std::string& errorMsg);
};

}  // namespace elog

#endif  // ELOG_ENABLE_CONFIG_SERVICE

#endif  // __ELOG_CONFIG_SERVICE_CLIENT_H__