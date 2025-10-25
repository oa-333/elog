#ifndef __ELOG_CONFIG_SERVICE_ETCD_PUBLISHER_H__
#define __ELOG_CONFIG_SERVICE_ETCD_PUBLISHER_H__

#ifdef ELOG_ENABLE_CONFIG_PUBLISH_ETCD

#include <string>
#include <vector>

#include "cfg_srv/elog_config_service_etcd_params.h"
#include "cfg_srv/elog_config_service_etcd_user.h"
#include "cfg_srv/elog_config_service_publisher.h"
#include "elog_common_def.h"
#include "elog_http_client.h"

namespace elog {

/** @brief Helper class for publishing the remote configuration service. */
class ELOG_API ELogConfigServiceEtcdPublisher : public ELogConfigServicePublisher,
                                                public ELogConfigServiceEtcdUser {
public:
    ELogConfigServiceEtcdPublisher()
        : ELogConfigServicePublisher("etcd"),
          ELogConfigServiceEtcdUser("etcd-config-service-publisher"),
          m_leaseId(0) {}
    ELogConfigServiceEtcdPublisher(ELogConfigServiceEtcdPublisher&) = delete;
    ELogConfigServiceEtcdPublisher(ELogConfigServiceEtcdPublisher&&) = delete;
    ELogConfigServiceEtcdPublisher& operator=(const ELogConfigServiceEtcdPublisher&) = delete;
    ~ELogConfigServiceEtcdPublisher() override {}

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

    /** @brief Embed headers in outgoing HTTP message. */
    void embedHeaders(httplib::Headers& headers) final;

    /**
     * @brief Handles HTTP POST result.
     * @param result The result to examine.
     * @return true If the result is regarded as success.
     * @return false If the result is regarded as failure, in which case the HTTP message will be
     * stored in a backlog for future attempt to resend to the server. Pay attention that when some
     * errors occur it does not make sense to resend, since the same error would occur again (e.g.
     * invalid payload, wrong endpoint name, etc.).
     */
    bool handleResult(const httplib::Result& result) final;

private:
    ELOG_DECLARE_CONFIG_SERVICE_PUBLISHER(ELogConfigServiceEtcdPublisher, etcd)

    // publish config service details key (first time after connect)
    bool publishConfigServiceV2(const char* value);

    // delete config service details key (before shutdown)
    void unpublishConfigServiceV2();

    // renew expiry/ttl of config service details key
    void renewExpiryV2();

    // publish config service details key (first time after connect)
    bool publishConfigServiceV3(const char* value);

    // delete config service details key (before shutdown)
    void unpublishConfigServiceV3();

    // renew expiry/ttl of config service details key
    void renewExpiryV3();

    // get authentication token
    bool getAuthToken();

    // contact etcd and grant lease
    bool grantLease(uint32_t expireSeconds, int64_t& leaseId);

    bool putKeyV3(const char* key, const char* value);

    // the key used for storing service details
    std::string m_serviceSpec;

    // authorization token (v3)
    std::string m_authToken;

    // ID of lease used with etcd v3
    int64_t m_leaseId;
};

}  // namespace elog

#endif  // ELOG_ENABLE_CONFIG_PUBLISH_ETCD

#endif  // __ELOG_CONFIG_SERVICE_ETCD_PUBLISHER_H__