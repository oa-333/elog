#ifndef __ELOG_REDIS_DB_TARGET_H__
#define __ELOG_REDIS_DB_TARGET_H__

#ifdef ELOG_ENABLE_REDIS_DB_CONNECTOR

#include <hiredis/hiredis.h>

#include "elog_db_target.h"

namespace elog {

class ELOG_API ELogRedisDbTarget : public ELogDbTarget {
public:
    // if maxThreads is zero, then the number configured during elog::initialize() will be used
    // the user is allowed here to override the value specified during elog::initialize()
    ELogRedisDbTarget(const ELogDbConfig& dbConfig, const std::string& host, int port,
                      const std::string& passwd, const std::vector<std::string>& indexInserts)
        : ELogDbTarget("Redis", dbConfig, ELogDbFormatter::QueryStyle::QS_PRINTF),
          m_host(host),
          m_port(port),
          m_passwd(passwd),
          m_indexInserts(indexInserts) {}

    ELogRedisDbTarget(const ELogRedisDbTarget&) = delete;
    ELogRedisDbTarget(ELogRedisDbTarget&&) = delete;
    ELogRedisDbTarget& operator=(const ELogRedisDbTarget&) = delete;

    ELOG_DECLARE_LOG_TARGET(ELogRedisDbTarget)

protected:
    /** @brief Performs target level initialization. */
    bool initDbTarget() final;

    /** @brief Performs target level termination. */
    void termDbTarget() final;

    /** @brief Allocates database access object. */
    void* allocDbData() final { return new (std::nothrow) RedisDbData(); }

    /** @brief Frees database access object. */
    void freeDbData(void* dbData) final { delete ((RedisDbData*)dbData); }

    /** @brief Initializes database access object. */
    bool connectDb(void* dbData) final;

    bool disconnectDb(void* dbData) final;

    /** @brief Sends a log record to a log target. */
    bool execInsert(const ELogRecord& logRecord, void* dbData) final;

private:
    std::string m_host;
    int m_port;
    std::string m_passwd;

    struct RedisDbData {
        redisContext* m_context;
        RedisDbData() : m_context(nullptr) {}
    };

    // prepare a formatter per each additional index statement
    std::vector<std::string> m_indexInserts;
    std::vector<ELogDbFormatter*> m_indexStmtFormatters;

    RedisDbData* validateConnectionState(void* dbData, bool shouldBeConnected);

    bool executeRedisCommand(RedisDbData* redisDbData, const char* cmd);

    void mergeQuotedTokens(const std::vector<std::string>& tokens,
                           std::vector<std::string>& cmdTokens);
};

}  // namespace elog

#endif  // ELOG_ENABLE_REDIS_DB_CONNECTOR

#endif  // __ELOG_REDIS_DB_TARGET_H__