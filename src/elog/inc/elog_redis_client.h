#ifndef __ELOG_REDIS_CLIENT_H__
#define __ELOG_REDIS_CLIENT_H__

#ifdef ELOG_USING_REDIS

#include <hiredis/hiredis.h>
#include <hiredis/hiredis_ssl.h>

#include <string>
#include <vector>

#include "elog_def.h"

namespace elog {

/** @brief Synchronous Redis client that support access to a cluster, as well as password/SSL. */
class ELOG_API ELogRedisClient {
public:
    ELogRedisClient()
        : m_usingSSL(false),
          m_redisContext(nullptr),
          m_sslContext(nullptr),
          m_currentServer((uint32_t)-1),
          m_connectionReady(false) {}
    ELogRedisClient(const ELogRedisClient&) = delete;
    ELogRedisClient(ELogRedisClient&&) = delete;
    ELogRedisClient& operator=(const ELogRedisClient&) = delete;
    ~ELogRedisClient() {}

    /**
     * @brief Adds a server to the redis server list.
     * @param serverAddr The server address string. The expected format is <host>:<port>.
     */
    inline void addServer(const char* host, int port) { m_serverList.push_back({host, port}); }

    /** @brief Sets the list of redis servers. */
    inline void setServerList(const std::vector<std::pair<std::string, int>>& serverList) {
        m_serverList = serverList;
    }

    /** @brief Sets password for redis login */
    inline void setPassword(const char* password) { m_password = password; }

    /**
     * @brief Sets SSL options.
     * @param opts The SSL options.
     */
    inline void setSSL(const redisSSLOptions& opts) {
        m_usingSSL = true;
        m_sslOptions = opts;
    }

    /** @brief Connects to any available Redis server. */
    bool connectRedis();

    /** @brief Disconnect from Redis server. */
    void disconnectRedis();

    /** @brief Queries whether connected to a Redis server. */
    bool isRedisConnected();

    /** @brief Execute a formatted command. */
    bool executeRedisCommand(const char* cmd);

    /** @brief Execute command via lambda visitor. */
    template <typename F>
    inline bool visitRedisCommand(F f) {
        redisReply* reply = (redisReply*)f(m_redisContext);
        return processReply(reply);
    }

    /** @brief Checks that reply is OK and has expected type. */
    bool checkReply(redisReply* reply, int expectedType = -1);

    /** @brief Extracts string from reply. */
    inline bool getStringReply(redisReply* reply, std::string& strReply) {
        if (checkReply(reply, REDIS_REPLY_STRING)) {
            strReply = reply->str;
            return true;
        }
        return false;
    }

    /** @brief Extracts integer from reply. */
    inline bool getIntegerReply(redisReply* reply, long long& intReply) {
        if (checkReply(reply, REDIS_REPLY_INTEGER)) {
            intReply = reply->integer;
            return true;
        }
        return false;
    }

private:
    std::vector<std::pair<std::string, int>> m_serverList;
    std::string m_password;
    bool m_usingSSL;
    redisSSLOptions m_sslOptions;
    redisContext* m_redisContext;
    redisSSLContext* m_sslContext;
    uint32_t m_currentServer;
    bool m_connectionReady;

    void mergeQuotedTokens(const std::vector<std::string>& tokens,
                           std::vector<std::string>& cmdTokens);

    bool processReply(redisReply* reply);
};

}  // namespace elog
#endif  // ELOG_USING_REDIS

#endif