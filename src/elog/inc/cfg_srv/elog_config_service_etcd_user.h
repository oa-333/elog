#ifndef __ELOG_CONFIG_SERVICE_ETCD_USER_H__
#define __ELOG_CONFIG_SERVICE_ETCD_USER_H__

#ifdef ELOG_ENABLE_CONFIG_PUBLISH_ETCD

#include <string>
#include <vector>

#include "cfg_srv/elog_config_service_etcd_params.h"
#include "cfg_srv/elog_config_service_user.h"
#include "elog_common_def.h"
#include "elog_http_client.h"

namespace elog {

/** @brief Helper class for publishing the remote configuration service. */
class ELOG_API ELogConfigServiceEtcdUser : public ELogConfigServiceUser,
                                           public ELogHttpClientAssistant {
public:
    ELogConfigServiceEtcdUser(const char* name)
        : ELogHttpClientAssistant(name), m_currServer(0), m_connected(false) {}
    ELogConfigServiceEtcdUser(ELogConfigServiceEtcdUser&) = delete;
    ELogConfigServiceEtcdUser(ELogConfigServiceEtcdUser&&) = delete;
    ELogConfigServiceEtcdUser& operator=(const ELogConfigServiceEtcdUser&) = delete;
    ~ELogConfigServiceEtcdUser() override {}

    /** @brief Configures the user */
    inline void configure(const ELogConfigServiceEtcdParams& params) { m_params = params; }

    /** @brief Sets the etcd API version. */
    inline void setApiVersion(ELogEtcdApiVersion apiVersion) { m_params.m_apiVersion = apiVersion; }

    /**
     * @brief Sets the key name and the rooted prefix path, under which the configuration service
     * will publish its details.
     * @param prefix The prefix (directory path) to use for publishing the remote configuration
     * service.
     * @param key The key to use for publishing the remote configuration service.
     * @note The actual path used for publishing the configuration service details is
     * <prefix>/<key>.
     */
    inline void setPrefixKey(const char* prefix, const char* key) {
        m_params.m_dirPrefix = prefix;
        m_params.m_key = key;
    }

    /**
     * @brief Sets the rooted prefix path, under which the configuration service will publish its
     * details.
     * @param prefix The prefix (directory path) to use for publishing the remote configuration
     * service.
     * @note The actual path used for publishing the configuration service details is
     * <prefix>/<key>.
     */
    inline void setPrefix(const char* prefix) { m_params.m_dirPrefix = prefix; }

    /**
     * @brief Sets the key, under which the configuration service will publish its details.
     * @param key The key to use for publishing the remote configuration service.
     * @note The actual path used for publishing the configuration service details is
     * <prefix>/<key>.
     */
    inline void setKey(const char* key) { m_params.m_key = key; }

    // TODO: check if etcd V2 supports SSL
    /** @brief Sets user/password for etcd login */
    inline void setUserPassword(const char* user, const char* password) {
        m_params.m_user = user;
        m_params.m_password = password;
    }

    /** @brief Sets user for etcd login */
    inline void setUser(const char* user) { m_params.m_user = user; }

    /** @brief Sets user/password for etcd login */
    inline void setPassword(const char* password) { m_params.m_password = password; }

    /** @brief Initializes the configuration service user. */
    bool initializeEtcd();

    /** @brief Terminates the configuration service user. */
    bool terminateEtcd();

    // query whether connected to service discovery server (key-value store)
    bool isEtcdConnected();

    // connect to service discovery server (key-value store)
    bool connectEtcd();

protected:
    ELogConfigServiceEtcdParams m_params;
    ELogHttpConfig m_httpConfig;
    std::string m_dir;
    ELogHttpClient m_etcdClient;
    uint32_t m_currServer;
    bool m_connected;

    // for basic authorization (V2 API only)
    std::string m_encodedCredentials;

    // access server list
    const ELogConfigServerList& getServerList() const final { return m_params.m_serverList; }
    ELogConfigServerList& modifyServerList() final { return m_params.m_serverList; }

    std::string encodeBase64(const std::string& inputStr);
    std::string decodeBase64(const std::string& encoded);
};

}  // namespace elog

#endif  // ELOG_ENABLE_CONFIG_PUBLISH_ETCD

#endif  // __ELOG_CONFIG_SERVICE_ETCD_USER_H__