#include "elog_mysql_db_target_provider.h"

#ifdef ELOG_ENABLE_MYSQL_DB_CONNECTOR

#include "elog_config_loader.h"
#include "elog_error.h"
#include "elog_mysql_db_target.h"

namespace elog {

ELogDbTarget* ELogMySqlDbTargetProvider::loadTarget(
    const std::string& logTargetCfg, const ELogTargetSpec& targetSpec,
    const std::string& connString, const std::string& insertQuery,
    ELogDbTarget::ThreadModel threadModel, uint32_t maxThreads, uint32_t reconnectTimeoutMillis) {
    // we expect 3 properties: db, user, password (optional)
    ELogPropertyMap::const_iterator itr = targetSpec.m_props.find("db");
    if (itr == targetSpec.m_props.end()) {
        ELOG_REPORT_ERROR(
            "Invalid mysql database log target specification, missing property db: %s",
            logTargetCfg.c_str());
        return nullptr;
    }
    const std::string& db = itr->second;

    itr = targetSpec.m_props.find("user");
    if (itr == targetSpec.m_props.end()) {
        ELOG_REPORT_ERROR(
            "Invalid mysql database log target specification, missing property user: %s",
            logTargetCfg.c_str());
        return nullptr;
    }
    const std::string& user = itr->second;

    itr = targetSpec.m_props.find("passwd");
    if (itr == targetSpec.m_props.end()) {
        ELOG_REPORT_ERROR(
            "Invalid mysql database log target specification, missing property passwd: %s",
            logTargetCfg.c_str());
        return nullptr;
    }
    const std::string& passwd = itr->second;
    ELogDbTarget* target = new (std::nothrow) ELogMySqlDbTarget(
        connString, db, user, passwd, insertQuery, threadModel, maxThreads, reconnectTimeoutMillis);
    if (target == nullptr) {
        ELOG_REPORT_ERROR("Failed to allocate MySQL log target, out of memory");
    }
    return target;
}

ELogDbTarget* ELogMySqlDbTargetProvider::loadTarget(const ELogConfigMapNode* logTargetCfg,
                                                    const std::string& connString,
                                                    const std::string& insertQuery,
                                                    ELogDbTarget::ThreadModel threadModel,
                                                    uint32_t maxThreads,
                                                    uint32_t reconnectTimeoutMillis) {
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
