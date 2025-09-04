#include "db/elog_mysql_db_target_provider.h"

#ifdef ELOG_ENABLE_MYSQL_DB_CONNECTOR

#include "db/elog_mysql_db_target.h"
#include "elog_config_loader.h"
#include "elog_report.h"

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogMySqlDbTargetProvider)

ELogDbTarget* ELogMySqlDbTargetProvider::loadTarget(const ELogConfigMapNode* logTargetCfg,
                                                    const std::string& connString,
                                                    const std::string& insertQuery,
                                                    ELogDbTarget::ThreadModel threadModel,
                                                    uint32_t maxThreads,
                                                    uint64_t reconnectTimeoutMillis) {
    // we expect 3 properties: db, user, password (optional)
    std::string db;
    if (!ELogConfigLoader::getLogTargetStringProperty(logTargetCfg, "MySQL", "db", db)) {
        return nullptr;
    }

    std::string user;
    if (!ELogConfigLoader::getLogTargetStringProperty(logTargetCfg, "MySQL", "user", user)) {
        return nullptr;
    }

    std::string passwd;
    if (!ELogConfigLoader::getLogTargetStringProperty(logTargetCfg, "MySQL", "passwd", passwd)) {
        return nullptr;
    }

    ELogDbTarget* target = new (std::nothrow) ELogMySqlDbTarget(
        connString, db, user, passwd, insertQuery, threadModel, maxThreads, reconnectTimeoutMillis);
    if (target == nullptr) {
        ELOG_REPORT_ERROR("Failed to allocate MySQL log target, out of memory");
    }
    return target;
}

}  // namespace elog

#endif  // ELOG_ENABLE_MYSQL_DB_CONNECTOR
