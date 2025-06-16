#include "elog_pgsql_db_target_provider.h"

#ifdef ELOG_ENABLE_PGSQL_DB_CONNECTOR

#include "elog_common.h"
#include "elog_config_loader.h"
#include "elog_error.h"
#include "elog_pgsql_db_target.h"

namespace elog {

ELogDbTarget* ELogPGSQLDbTargetProvider::loadTarget(
    const std::string& logTargetCfg, const ELogTargetSpec& targetSpec,
    const std::string& connString, const std::string& insertQuery,
    ELogDbTarget::ThreadModel threadModel, uint32_t maxThreads, uint32_t reconnectTimeoutMillis) {
    // we expect 4 properties: db, port, user, passwd (optional)
    // the connection string actually contains the host name/ip
    ELogPropertyMap::const_iterator itr = targetSpec.m_props.find("db");
    if (itr == targetSpec.m_props.end()) {
        ELOG_REPORT_ERROR(
            "Invalid PostgreSQL database log target specification, missing property db: %s",
            logTargetCfg.c_str());
        return nullptr;
    }
    const std::string& db = itr->second;

    itr = targetSpec.m_props.find("port");
    if (itr == targetSpec.m_props.end()) {
        ELOG_REPORT_ERROR(
            "Invalid PostgreSQL database log target specification, missing property port: %s",
            logTargetCfg.c_str());
        return nullptr;
    }
    uint32_t port = 0;
    if (!parseIntProp("port", logTargetCfg, itr->second, port, true)) {
        ELOG_REPORT_ERROR("Invalid PostgreSQL database log target specification, invalid port: %s",
                          logTargetCfg.c_str());
        return nullptr;
    }

    itr = targetSpec.m_props.find("user");
    if (itr == targetSpec.m_props.end()) {
        ELOG_REPORT_ERROR(
            "Invalid PostgreSQL database log target specification, missing property user: %s",
            logTargetCfg.c_str());
        return nullptr;
    }
    const std::string& user = itr->second;

    itr = targetSpec.m_props.find("passwd");
    if (itr == targetSpec.m_props.end()) {
        ELOG_REPORT_ERROR(
            "Invalid PostgreSQL database log target specification, missing property passwd: %s",
            logTargetCfg.c_str());
        return nullptr;
    }
    const std::string& passwd = itr->second;
    ELogDbTarget* target =
        new (std::nothrow) ELogPGSQLDbTarget(connString, port, db, user, passwd, insertQuery,
                                             threadModel, maxThreads, reconnectTimeoutMillis);
    if (target == nullptr) {
        ELOG_REPORT_ERROR("Failed to allocate PostgreSQL log target, out of memory");
    }
    return target;
}

ELogDbTarget* ELogPGSQLDbTargetProvider::loadTarget(const ELogConfigMapNode* logTargetCfg,
                                                    const std::string& connString,
                                                    const std::string& insertQuery,
                                                    ELogDbTarget::ThreadModel threadModel,
                                                    uint32_t maxThreads,
                                                    uint32_t reconnectTimeoutMillis) {
    // we expect 4 properties: db, port, user, passwd (optional)
    // the connection string actually contains the host name/ip
    std::string db;
    if (!ELogConfigLoader::getLogTargetStringProperty(logTargetCfg, "PostgreSQL", "db", db)) {
        return nullptr;
    }

    int64_t port = 0;
    if (!ELogConfigLoader::getLogTargetIntProperty(logTargetCfg, "PostgreSQL", "port", port)) {
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
