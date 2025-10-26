#ifndef __ELOG_CONFIG_SERVICE_REDIS_PUBLISHER_H__
#define __ELOG_CONFIG_SERVICE_REDIS_PUBLISHER_H__

#ifdef ELOG_ENABLE_CONFIG_PUBLISH_REDIS

#include <string>
#include <vector>

#include "cfg_srv/elog_config_service_publisher.h"
#include "cfg_srv/elog_config_service_redis_params.h"
#include "cfg_srv/elog_config_service_redis_user.h"
#include "elog_common_def.h"
#include "elog_redis_client.h"

namespace elog {

/** @brief Helper class for publishing the remote configuration service. */
class ELOG_API ELogConfigServiceRedisPublisher : public ELogConfigServicePublisher,
                                                 public ELogConfigServiceRedisUser {
public:
    ELogConfigServiceRedisPublisher() : ELogConfigServicePublisher("redis") {}
    ELogConfigServiceRedisPublisher(ELogConfigServiceRedisPublisher&) = delete;
    ELogConfigServiceRedisPublisher(ELogConfigServiceRedisPublisher&&) = delete;
    ELogConfigServiceRedisPublisher& operator=(const ELogConfigServiceRedisPublisher&) = delete;

    /**
     * @brief Creates a redis publisher object.
     * @note Since the destructor is private, this publisher can be created only in this way. This
     * was done by design, in order to avoid allocating objects in one module and releasing them in
     * another module.
     * @note When done, dispose of the object by calling @ref
     * ELogConfigServiceEtcdPublisher::destroy() or @ref destroyConfigServicePublisher().
     */
    static ELogConfigServiceRedisPublisher* create();

    /** @brief Static destructor (enforce same module of allocation/deallocation). */
    static void destroy(ELogConfigServiceRedisPublisher* publisher);

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

    // publish config service details key (first time after connect)
    bool publishConfigService() final;

    // delete config service details key (before shutdown)
    void unpublishConfigService() final;

    // renew expiry/ttl of config service details key
    void renewExpiry() final;

    // query whether connected to service discovery server (key-value store)
    bool isConnected() final;

    // connect to service discovery server (key-value store)
    bool connect() final;

private:
    std::string m_serviceSpec;

    ELOG_DECLARE_CONFIG_SERVICE_PUBLISHER(ELogConfigServiceRedisPublisher, redis, ELOG_API)
};

}  // namespace elog

#endif  // ELOG_ENABLE_CONFIG_PUBLISH_REDIS

#endif  // __ELOG_CONFIG_SERVICE_REDIS_PUBLISHER_H__