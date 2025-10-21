#ifndef __ELOG_CONFIG_SERVICE_REDIS_READER_H__
#define __ELOG_CONFIG_SERVICE_REDIS_READER_H__

#ifdef ELOG_ENABLE_CONFIG_PUBLISH_REDIS

#include <string>
#include <unordered_map>

#include "elog_common_def.h"
#include "elog_config_service_redis_params.h"
#include "elog_redis_client.h"

namespace elog {

typedef std::unordered_map<std::string, std::string> ELogConfigServiceMap;

/** @brief Helper class for reading remote configuration service details from redis. */
class ELOG_API ELogConfigServiceRedisReader {
public:
    ELogConfigServiceRedisReader() {}
    ELogConfigServiceRedisReader(ELogConfigServiceRedisReader&) = delete;
    ELogConfigServiceRedisReader(ELogConfigServiceRedisReader&&) = delete;
    ELogConfigServiceRedisReader& operator=(const ELogConfigServiceRedisReader&) = delete;
    ~ELogConfigServiceRedisReader() {}

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

    /**
     * @brief Sets the list of redis servers (comma or semicolon-separated list of strings, each
     * string in the form <host>:<port>).
     */
    inline bool setServerList(const std::string& serverList) {
        return parseServerListString(serverList);
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

    /** @brief Initializes the configuration service publisher. */
    bool initialize();

    /** @brief Terminates the configuration service publisher. */
    bool terminate();

    /**
     * @brief Lists all services registered under the configured prefix/key.
     * @param serviceMap The resulting service map, mapping from <host>:<ip> to application name.
     * @return The operations's result.
     */
    bool listServices(ELogConfigServiceMap& serviceMap);

private:
    // TODO: refactor publisher and reader's common code into some parent
    ELogConfigServiceRedisParams m_params;
    ELogRedisClient m_redisClient;

    bool parseServerListString(const std::string& serverListStr);

    bool checkScanReply(redisReply* reply, uint64_t& cursor);
    void readScanCursor(redisReply* reply, uint64_t cursor, ELogConfigServiceMap& serviceMap);
};

}  // namespace elog

#endif  // ELOG_ENABLE_CONFIG_PUBLISH_REDIS

#endif  // __ELOG_CONFIG_SERVICE_REDIS_READER_H__