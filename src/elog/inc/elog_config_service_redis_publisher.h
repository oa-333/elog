#ifndef __ELOG_CONFIG_SERVICE_REDIS_PUBLISHER_H__
#define __ELOG_CONFIG_SERVICE_REDIS_PUBLISHER_H__

#ifdef ELOG_ENABLE_CONFIG_PUBLISH_REDIS

#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "elog_common_def.h"
#include "elog_config_service_publisher.h"
#include "elog_config_service_redis_params.h"
#include "elog_redis_client.h"

namespace elog {

/** @brief Helper class for publishing the remote configuration service. */
class ELOG_API ELogConfigServiceRedisPublisher : public ELogConfigServicePublisher {
public:
    ELogConfigServiceRedisPublisher()
        : ELogConfigServicePublisher("redis"), m_requiresPublish(true), m_stopPublish(false) {}
    ELogConfigServiceRedisPublisher(ELogConfigServiceRedisPublisher&) = delete;
    ELogConfigServiceRedisPublisher(ELogConfigServiceRedisPublisher&&) = delete;
    ELogConfigServiceRedisPublisher& operator=(const ELogConfigServiceRedisPublisher&) = delete;
    ~ELogConfigServiceRedisPublisher() override {}

    /** @brief Configures the publisher */
    inline void configure(const ELogConfigServiceRedisParams& params) { m_params = params; }

    /**
     * @brief Adds a server to the redis server list.
     * @param host The server's host name.
     * @param port The server's port.
     */
    inline void addServer(const char* host, int port) {
        m_params.m_serverList.push_back({host, port});
    }

    /** @brief Sets the list of redis servers. */
    inline void setServerList(const std::vector<std::pair<std::string, int>>& serverList) {
        m_params.m_serverList = serverList;
    }

    /**
     * @brief Sets the key name under which the configuration service will publish its details.
     * @param key The key to use for publishing the remote configuration service.
     */
    inline void setKey(const char* key) { m_params.m_key = key; }

    /** @brief Sets password for redis login */
    inline void setPassword(const char* user, const char* password) {
        m_params.m_password = password;
    }

    /** @brief Sets SSL options, as defined by the Redis C Client API (hiredis). */
    void setSSLOptions(const char* caCertFileName, const char* caPath, const char* certFileName,
                       const char* privateKeyFileName, const char* serverName,
                       ELogRedisSslVerifyMode m_verifyMode);

    /** @brief Loads configuration service publisher from configuration. */
    bool load(const ELogConfigMapNode* cfg) override;

    /** @brief Loads configuration service publisher from properties. */
    bool load(const ELogPropertySequence& props) override;

    /** @brief Initializes the configuration service publisher. */
    bool initialize() override;

    /** @brief Terminates the configuration service publisher. */
    bool terminate() override;

    /**
     * @brief Notifies the publisher that the remote configuration service connection details
     * can be published. Normally this means registering the configuration service at some
     * global service registry for discovery purposes.
     * @param host The interface on which the remote configuration service is listening for
     * incoming connections.
     * @param port The port number on which the remote configuration service is listening for
     * incoming connections.
     */
    void onConfigServiceStart(const char* host, int port) override;

    /**
     * @brief Notifies the publisher that the remote configuration service is stopping. Normally
     * this means deregistering the configuration service from some global service registry for
     * discovery purposes.
     * @param host The interface on which the remote configuration service is listening for
     * incoming connections.
     * @param port The port number on which the remote configuration service is listening for
     * incoming connections.
     */
    void onConfigServiceStop(const char* host, int port) override;

private:
    ELogConfigServiceRedisParams m_params;
    ELogRedisClient m_redisClient;
    std::string m_serviceSpec;
    std::thread m_publishThread;
    std::mutex m_lock;
    std::condition_variable m_cv;
    bool m_requiresPublish;
    bool m_stopPublish;

    bool parseServerListString(const std::string& serverListStr);
    void publishThread();
    void execPublishService();
    void publishConfigService();
    void unpublishConfigService();
    void renewExpiry();

    ELOG_DECLARE_CONFIG_SERVICE_PUBLISHER(ELogConfigServiceRedisPublisher, redis)
};

}  // namespace elog

#endif  // ELOG_ENABLE_CONFIG_PUBLISH_REDIS

#endif  // __ELOG_CONFIG_SERVICE_REDIS_PUBLISHER_H__