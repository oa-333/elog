#ifndef __ELOG_CONFIG_SERVICE_REDIS_PARAMS_H__
#define __ELOG_CONFIG_SERVICE_REDIS_PARAMS_H__

#ifdef ELOG_ENABLE_CONFIG_PUBLISH_REDIS

#include <string>
#include <vector>

#include "cfg_srv/elog_config_server_details.h"
#include "elog_common_def.h"

namespace elog {

/**
 * @brief Verification modes, as defined in the Redis SSL client (but not necessarily the same
 * value as defined in Redis headers).
 */
enum class ELogRedisSslVerifyMode : uint32_t {
    /** @brief Equivalent to REDIS_SSL_VERIFY_NONE. */
    VM_NONE,

    /** @brief Equivalent to REDIS_SSL_VERIFY_PEER. */
    VM_PEER,

    /** @brief Equivalent to REDIS_SSL_VERIFY_FAIL_IF_NO_PEER_CERT. */
    VM_FAIL_IF_NO_PEER_CERT,

    /** @brief Equivalent to REDIS_SSL_VERIFY_CLIENT_ONCE. */
    VM_CLIENT_ONCE,

    /** @brief Equivalent to REDIS_SSL_VERIFY_POST_HANDSHAKE. */
    VM_POST_HANDSHAKE
};

extern ELOG_API int convertVerifyMode(ELogRedisSslVerifyMode verifyMode);

/** @struct Remote Configuration Service publisher for redis parameters. */
struct ELOG_API ELogConfigServiceRedisParams {
    /** @brief List of redis servers (host,port). */
    ELogConfigServerList m_serverList;

    /** @brief Key name for redis. */
    std::string m_key;

    /** @brief Optional password for redis login. */
    std::string m_password;

    /** @brief SSL options as defined by the Redis SSL C Client API (hiredis). */
    bool m_usingSSL;
    std::string m_caCertFileName;
    std::string m_caPath;
    std::string m_certFileName;
    std::string m_privateKeyFileName;
    std::string m_serverName;
    ELogRedisSslVerifyMode m_verifyMode;

    /** @brief The expiry timeout in seconds associated with the redis key. */
    uint32_t m_expirySeconds;

    /** @brief The timeout for renewing the expiry of the redis key. */
    uint32_t m_renewExpiryTimeoutSeconds;

    ELogConfigServiceRedisParams()
        : m_key(ELOG_DEFAULT_REDIS_KEY),
          m_usingSSL(false),
          m_verifyMode(ELogRedisSslVerifyMode::VM_NONE),
          m_expirySeconds(ELOG_DEFAULT_REDIS_EXPIRY_SECONDS),
          m_renewExpiryTimeoutSeconds(ELOG_DEFAULT_REDIS_EXPIRY_RENEW_SECONDS) {}
    ELogConfigServiceRedisParams(const ELogConfigServiceRedisParams&) = default;
    ELogConfigServiceRedisParams(ELogConfigServiceRedisParams&&) = default;
    ELogConfigServiceRedisParams& operator=(const ELogConfigServiceRedisParams&) = default;
    ~ELogConfigServiceRedisParams() {}
};

}  // namespace elog

#endif  // ELOG_ENABLE_CONFIG_PUBLISH_REDIS

#endif  // __ELOG_CONFIG_SERVICE_REDIS_PARAMS_H__