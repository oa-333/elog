#ifndef __ELOG_CONFIG_SERVICE_H__
#define __ELOG_CONFIG_SERVICE_H__

#ifdef ELOG_ENABLE_CONFIG_SERVICE

#include <msg/msg_server.h>
#include <transport/tcp_server.h>

#include "cfg_srv/elog_config_service_publisher.h"
#include "elog_def.h"
#include "elog_proto.h"
#include "elog_rolling_bitset.h"

namespace elog {

/** @brief Remote configuration service. */
class ELogConfigService : public commutil::MsgFrameListener {
public:
    /** @brief Creates the single instance of the remote configuration service. */
    static bool createInstance();

    /** @brief Destroys the single instance of the remote configuration service. */
    static bool destroyInstance();

    /** @brief Retrieves the single instance of the remote configuration service. */
    static ELogConfigService* getInstance();

    /**
     * @brief Initializes the ELog configuration server.
     * @param iface The interface to listen on. Specify "0.0.0.0" to listen on all interfaces.
     * @param port The port to listen on. Specify zero to choose any port.
     * @param publisher Optionally specify a publisher used to publish the remote configuration
     * service details and register it in some global service registry (e.g. etcd, Consul, etc.).
     * @return The operation result.
     */
    commutil::ErrorCode initialize(const char* iface, int port,
                                   ELogConfigServicePublisher* publisher = nullptr);

    /**
     * @brief Configures the listen address for the ELog configuration server.
     *
     * @note The caller is responsible for managing calls to start/stop or restart.
     *
     * @param iface The interface to listen on. Specify "0.0.0.0" to listen on all interfaces.
     * @param port The port to listen on. Specify zero to choose any port.
     */
    void setListenAddress(const char* iface, int port);

    /**
     * @brief Sets the configuration service publisher.
     *
     * @note The caller is responsible for the life-cycle of the publisher. The configuration
     * service will still call the publisher's @ref ELogConfigServicePublisher::terminate() method
     * if it is still present during configuration service termination.
     */
    inline void setPublisher(ELogConfigServicePublisher* publisher) { m_publisher = publisher; }

    /** @brief Releases all resources allocated for recovery. */
    commutil::ErrorCode terminate();

    /** @brief Starts the configuration service. */
    commutil::ErrorCode start();

    /** @brief Stops the configuration service. */
    commutil::ErrorCode stop();

    /** @brief Restart the configuration service. */
    commutil::ErrorCode restart();

    /** @brief Queries whether the configuration service is running. */
    bool isRunning() {
        // delegate message server
        return m_msgServer.isRunning();
    }

protected:
    /**
     * @brief Implements MsgFrameListener interface. Handles incoming message buffer provided by the
     * framing protocol.
     * @param connectionDetails The client's connection details.
     * @param msgHeader The header of the incoming meta-message.
     * @param msgBuffer The message buffer. Valid only if status is zero.
     * @param bufferSize The buffer length. Valid only if status is zero.
     * @param lastInBatch Designates whether this is the last message within a message batch. In
     * case of a single message this is always true.
     * @param batchSize The number of messages in the message batch. In case of a single message
     * this is always 1.
     * @return Message handling result. If message handling within a batch should continue then
     * return @ref E_OK, otherwise deriving sub-classes should return error code (e.g. irrecoverable
     * deserialization error), in which case @ref handleMsgError() is NOT called, and if some error
     * status needs to be sent to the client, then deriving sub-classes are responsible for that.
     */
    commutil::ErrorCode handleMsg(const commutil::ConnectionDetails& connectionDetails,
                                  const commutil::MsgHeader& msgHeader, const char* msgBuffer,
                                  uint32_t bufferSize, bool lastInBatch,
                                  uint32_t batchSize) override;

    /**
     * @brief Implements MsgFrameListener interface. Handle errors during message unpacking.
     * @param connectionDetails The client's connection details.
     * @param msgHeader The header of the incoming meta-message.
     * @param status Deserialization error status.
     */
    void handleMsgError(const commutil::ConnectionDetails& connDetails,
                        const commutil::MsgHeader& msgHeader, int status) override;

private:
    ELogConfigService() : m_publisher(nullptr) {}
    ELogConfigService(const ELogConfigService&) = delete;
    ELogConfigService(ELogConfigService&&) = delete;
    ELogConfigService& operator=(ELogConfigService&) = delete;
    ~ELogConfigService() {}

    commutil::ErrorCode handleConfigLevelQueryMsg(
        const commutil::ConnectionDetails& connectionDetails, const commutil::MsgHeader& msgHeader,
        const char* msgBuffer, uint32_t bufferSize, bool lastInBatch, uint32_t batchSize);

    commutil::ErrorCode handleConfigLevelUpdateMsg(
        const commutil::ConnectionDetails& connectionDetails, const commutil::MsgHeader& msgHeader,
        const char* msgBuffer, uint32_t bufferSize, bool lastInBatch, uint32_t batchSize);

    commutil::ErrorCode sendResponse(const commutil::ConnectionDetails& connectionDetails,
                                     const commutil::MsgHeader& msgHeader, uint16_t msgId,
                                     const ::google::protobuf::Message& msg);

    void sendReplyError(const commutil::ConnectionDetails& connectionDetails,
                        const commutil::MsgHeader& msgHeader, int status, const char* errorMsg);

    static ELogConfigService* sInstance;
    commutil::TcpServer m_tcpServer;
    commutil::MsgServer m_msgServer;
    ELogConfigServicePublisher* m_publisher;
};

}  // namespace elog

#endif  // ELOG_ENABLE_CONFIG_SERVICE

#endif  // __ELOG_CONFIG_SERVICE_H__