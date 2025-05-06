#include "elog_pgsql_db_target_provider.h"

#ifdef ELOG_ENABLE_PGSQL_DB_CONNECTOR

#include "elog_common.h"
#include "elog_pgsql_db_target.h"
#include "elog_system.h"

namespace elog {

ELogDbTarget* ELogPGSQLDbTargetProvider::loadTarget(const std::string& logTargetCfg,
                                                    const ELogTargetSpec& targetSpec,
                                                    const std::string& connString,
                                                    const std::string& insertQuery) {
    // we expect 4 properties: db, port, user, passwd (optional)
    // the connection string actually contains the host name/ip
    ELogPropertyMap::const_iterator itr = targetSpec.m_props.find("db");
    if (itr == targetSpec.m_props.end()) {
        ELogSystem::reportError(
            "Invalid PostgreSQL database log target specification, missing property db: %s",
            logTargetCfg.c_str());
        return nullptr;
    }
    const std::string& db = itr->second;

    itr = targetSpec.m_props.find("port");
    if (itr == targetSpec.m_props.end()) {
        ELogSystem::reportError(
            "Invalid PostgreSQL database log target specification, missing property port: %s",
            logTargetCfg.c_str());
        return nullptr;
    }
    uint32_t port = 0;
    if (!parseIntProp("port", logTargetCfg, itr->second, port, true)) {
        ELogSystem::reportError(
            "Invalid PostgreSQL database log target specification, invalid port: %s",
            logTargetCfg.c_str());
        return nullptr;
    }

    itr = targetSpec.m_props.find("user");
    if (itr == targetSpec.m_props.end()) {
        ELogSystem::reportError(
            "Invalid PostgreSQL database log target specification, missing property user: %s",
            logTargetCfg.c_str());
        return nullptr;
    }
    const std::string& user = itr->second;

    itr = targetSpec.m_props.find("passwd");
    if (itr == targetSpec.m_props.end()) {
        ELogSystem::reportError(
            "Invalid PostgreSQL database log target specification, missing property passwd: %s",
            logTargetCfg.c_str());
        return nullptr;
    }
    const std::string& passwd = itr->second;
    ELogDbTarget* target =
        new (std::nothrow) ELogPGSQLDbTarget(connString, port, db, user, passwd, insertQuery);
    if (target == nullptr) {
        ELogSystem::reportError("Failed to allocate PostgreSQL log target, out of memory");
    }
    return target;
}

}  // namespace elog

#endif  // ELOG_ENABLE_PGSQL_DB_CONNECTOR
