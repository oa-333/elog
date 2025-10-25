#ifndef __ELOG_CONFIG_SERVICE_REDIS_READER_H__
#define __ELOG_CONFIG_SERVICE_REDIS_READER_H__

#ifdef ELOG_ENABLE_CONFIG_PUBLISH_REDIS

#include <string>
#include <unordered_map>

#include "cfg_srv/elog_config_service_reader.h"
#include "cfg_srv/elog_config_service_redis_params.h"
#include "cfg_srv/elog_config_service_redis_user.h"
#include "elog_common_def.h"
#include "elog_redis_client.h"

namespace elog {

typedef std::unordered_map<std::string, std::string> ELogConfigServiceMap;

/** @brief Helper class for reading remote configuration service details from redis. */
class ELOG_API ELogConfigServiceRedisReader : public ELogConfigServiceReader,
                                              public ELogConfigServiceRedisUser {
public:
    ELogConfigServiceRedisReader() {}
    ELogConfigServiceRedisReader(ELogConfigServiceRedisReader&) = delete;
    ELogConfigServiceRedisReader(ELogConfigServiceRedisReader&&) = delete;
    ELogConfigServiceRedisReader& operator=(const ELogConfigServiceRedisReader&) = delete;
    ~ELogConfigServiceRedisReader() final {}

    /** @brief Initializes the configuration service publisher. */
    bool initialize() final;

    /** @brief Terminates the configuration service publisher. */
    bool terminate() final;

    /**
     * @brief Lists all services registered under the configured prefix/key.
     * @param serviceMap The resulting service map, mapping from <host>:<ip> to application name.
     * @return The operations's result.
     */
    bool listServices(ELogConfigServiceMap& serviceMap) final;

private:
    bool checkScanReply(redisReply* reply, uint64_t& cursor);
    void readScanCursor(redisReply* reply, uint64_t cursor, ELogConfigServiceMap& serviceMap);
};

}  // namespace elog

#endif  // ELOG_ENABLE_CONFIG_PUBLISH_REDIS

#endif  // __ELOG_CONFIG_SERVICE_REDIS_READER_H__