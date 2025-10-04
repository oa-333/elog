#include "db/elog_redis_db_target_provider.h"

#ifdef ELOG_ENABLE_REDIS_DB_CONNECTOR

#include "db/elog_redis_db_target.h"
#include "elog_common.h"
#include "elog_config_loader.h"
#include "elog_config_parser.h"
#include "elog_report.h"

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogRedisDbTargetProvider)

ELogDbTarget* ELogRedisDbTargetProvider::loadTarget(const ELogConfigMapNode* logTargetCfg,
                                                    const std::string& connString,
                                                    const std::string& insertQuery,
                                                    ELogDbTarget::ThreadModel threadModel,
                                                    uint32_t maxThreads,
                                                    uint64_t reconnectTimeoutMillis) {
    // the connection string actually contains the host name/ip
    std::string host;
    int port;
    if (!ELogConfigParser::parseHostPort(connString, host, port)) {
        ELOG_REPORT_ERROR("Invalid redis log target connection string, expecting <host:port>: %s",
                          connString.c_str());
        return nullptr;
    }

    std::string passwd;
    if (!ELogConfigLoader::getOptionalLogTargetStringProperty(logTargetCfg, "redis", "passwd",
                                                              passwd)) {
        return nullptr;
    }

    std::string indexInserts;
    if (!ELogConfigLoader::getOptionalLogTargetStringProperty(logTargetCfg, "redis", "index_insert",
                                                              indexInserts)) {
        return nullptr;
    }

    // break down insert query into sub-queries: first for insert, others for index insert
    std::vector<std::string> insertStmts;
    tokenize(indexInserts.c_str(), insertStmts, ";");

    ELogDbTarget* target =
        new (std::nothrow) ELogRedisDbTarget(host, port, passwd, insertQuery, insertStmts,
                                             threadModel, maxThreads, reconnectTimeoutMillis);
    if (target == nullptr) {
        ELOG_REPORT_ERROR("Failed to allocate Redis log target, out of memory");
    }
    return target;
}

}  // namespace elog

#endif  // ELOG_ENABLE_REDIS_DB_CONNECTOR
