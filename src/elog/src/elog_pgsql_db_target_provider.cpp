#include "elog_pgsql_db_target_provider.h"

#ifdef ELOG_ENABLE_PGSQL_DB_CONNECTOR

#include "elog_common.h"
#include "elog_config_loader.h"
#include "elog_pgsql_db_target.h"
#include "elog_report.h"

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogPGSQLDbTargetProvider)

ELogDbTarget* ELogPGSQLDbTargetProvider::loadTarget(const ELogConfigMapNode* logTargetCfg,
                                                    const std::string& connString,
                                                    const std::string& insertQuery,
                                                    ELogDbTarget::ThreadModel threadModel,
                                                    uint32_t maxThreads,
                                                    uint64_t reconnectTimeoutMillis) {
    // we expect 4 properties: db, port, user, passwd (optional)
    // the connection string actually contains the host name/ip
    std::string db;
    if (!ELogConfigLoader::getLogTargetStringProperty(logTargetCfg, "PostgreSQL", "db", db)) {
        return nullptr;
    }

    uint32_t port = 0;
    if (!ELogConfigLoader::getLogTargetUInt32Property(logTargetCfg, "PostgreSQL", "port", port)) {
        return nullptr;
    }

    std::string user;
    if (!ELogConfigLoader::getLogTargetStringProperty(logTargetCfg, "PostgreSQL", "user", user)) {
        return nullptr;
    }

    std::string passwd;
    if (!ELogConfigLoader::getLogTargetStringProperty(logTargetCfg, "PostgreSQL", "passwd",
                                                      passwd)) {
        return nullptr;
    }

    ELogDbTarget* target =
        new (std::nothrow) ELogPGSQLDbTarget(connString, port, db, user, passwd, insertQuery,
                                             threadModel, maxThreads, reconnectTimeoutMillis);
    if (target == nullptr) {
        ELOG_REPORT_ERROR("Failed to allocate PostgreSQL log target, out of memory");
    }
    return target;
}

}  // namespace elog

#endif  // ELOG_ENABLE_PGSQL_DB_CONNECTOR
