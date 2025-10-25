#ifndef __ELOG_CONFIG_SERVICE_REDIS_USER_H__
#define __ELOG_CONFIG_SERVICE_REDIS_USER_H__

#ifdef ELOG_ENABLE_CONFIG_PUBLISH_REDIS

#include <string>
#include <vector>

#include "cfg_srv/elog_config_service_redis_params.h"
#include "cfg_srv/elog_config_service_user.h"
#include "elog_common_def.h"
#include "elog_redis_client.h"

namespace elog {

/** @brief Helper class for reading or publishing remote configuration service details. */
class ELOG_API ELogConfigServiceRedisUser : public ELogConfigServiceUser {
public:
    ELogConfigServiceRedisUser() {}
    ELogConfigServiceRedisUser(ELogConfigServiceRedisUser&) = delete;
    ELogConfigServiceRedisUser(ELogConfigServiceRedisUser&&) = delete;
    ELogConfigServiceRedisUser& operator=(const ELogConfigServiceRedisUser&) = delete;
    ~ELogConfigServiceRedisUser() override {}

    /** @brief Configures the user */
    inline void configure(const ELogConfigServiceRedisParams& params) { m_params = params; }

    /**
     * @brief Sets the key name under which the configuration service will publish its details.
     * @param key The key to use for publishing the remote configuration service.
     */
    inline void setKey(const char* key) { m_params.m_key = key; }

    /** @brief Sets password for redis login */
    inline void setPassword(const char* password) { m_params.m_password = password; }

    /** @brief Sets SSL options, as defined by the Redis C Client API (hiredis). */
    void setSSLOptions(const char* caCertFileName, const char* caPath, const char* certFileName,
                       const char* privateKeyFileName, const char* serverName,
                       ELogRedisSslVerifyMode m_verifyMode);

    /** @brief Initializes the configuration service user. */
    bool initializeRedis();

    /** @brief Terminates the configuration service user. */
    bool terminateRedis();

    // query whether connected to service discovery server (key-value store)
    bool isRedisConnected();

    // connect to service discovery server (key-value store)
    bool connectRedis();

protected:
    ELogConfigServiceRedisParams m_params;
    ELogRedisClient m_redisClient;

    // access server list
    const ELogConfigServerList& getServerList() const final { return m_params.m_serverList; }
    ELogConfigServerList& modifyServerList() final { return m_params.m_serverList; }
};

}  // namespace elog

#endif  // ELOG_ENABLE_CONFIG_PUBLISH_REDIS

#endif  // __ELOG_CONFIG_SERVICE_REDIS_USER_H__