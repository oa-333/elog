#ifndef __ELOG_CONFIG_SERVICE_PARAMS_ETCD_H__
#define __ELOG_CONFIG_SERVICE_PARAMS_ETCD_H__

#ifdef ELOG_ENABLE_CONFIG_PUBLISH_ETCD

#include <string>
#include <vector>

#include "cfg_srv/elog_config_server_details.h"
#include "elog_common_def.h"

namespace elog {

/** @brief etcd API version constants. */
enum class ELogEtcdApiVersion : uint32_t {
    /** @brief Use API version 2. */
    ELOG_ETCD_API_V2,

    /** @brief Use API version 3. */
    ELOG_ETCD_API_V3
};

/** @brief Converts etcd API version from string to enumerated value. */
extern ELOG_API bool convertEtcdApiVersion(const char* apiVersionStr,
                                           ELogEtcdApiVersion& apiVersion);

/** @struct Remote Configuration Service publisher for etcd parameters. */
struct ELOG_API ELogConfigServiceEtcdParams {
    /** @brief List of etcd servers (host:port). */
    ELogConfigServerList m_serverList;

    /** @brief etcd API version. */
    ELogEtcdApiVersion m_apiVersion;

    /** @brief Directory prefix for etcd. */
    std::string m_dirPrefix;

    /** @brief Key name for etcd. */
    std::string m_key;

    /** @brief Optional user for etcd login. */
    std::string m_user;

    /** @brief Optional password for etcd login. */
    std::string m_password;

    /** @brief The expiry timeout in seconds associated with the etcd key. */
    uint32_t m_expirySeconds;

    /** @brief The timeout for renewing the expiry of the etcd key. */
    uint32_t m_renewExpiryTimeoutSeconds;

    ELogConfigServiceEtcdParams()
        : m_apiVersion(ELogEtcdApiVersion::ELOG_ETCD_API_V3),
          m_dirPrefix(ELOG_DEFAULT_ETCD_PREFIX),
          m_key(ELOG_DEFAULT_ETCD_KEY),
          m_expirySeconds(ELOG_DEFAULT_ETCD_EXPIRY_SECONDS),
          m_renewExpiryTimeoutSeconds(ELOG_DEFAULT_ETCD_EXPIRY_RENEW_SECONDS) {}
    ELogConfigServiceEtcdParams(const ELogConfigServiceEtcdParams&) = default;
    ELogConfigServiceEtcdParams(ELogConfigServiceEtcdParams&&) = default;
    ELogConfigServiceEtcdParams& operator=(const ELogConfigServiceEtcdParams&) = default;
    ~ELogConfigServiceEtcdParams() {}
};

}  // namespace elog

#endif  // ELOG_ENABLE_CONFIG_PUBLISH_ETCD

#endif  // __ELOG_CONFIG_SERVICE_PARAMS_ETCD_H__