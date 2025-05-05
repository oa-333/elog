#include "elog_mysql_db_handler.h"

#ifdef ELOG_ENABLE_MYSQL_DB_CONNECTOR

#include "elog_mysql_db_target.h"
#include "elog_system.h"

namespace elog {

ELogTarget* ELogMySqlDbHandler::loadTarget(const std::string& logTargetCfg,
                                           const ELogTargetSpec& targetSpec,
                                           const std::string& connString,
                                           const std::string& insertQuery) {
    // we expect 3 properties: db, user, password (optional)
    ELogPropertyMap::const_iterator itr = targetSpec.m_props.find("db");
    if (itr == targetSpec.m_props.end()) {
        ELogSystem::reportError(
            "Invalid mysql database log target specification, missing property db: %s",
            logTargetCfg.c_str());
        return nullptr;
    }
    const std::string& db = itr->second;

    itr = targetSpec.m_props.find("user");
    if (itr == targetSpec.m_props.end()) {
        ELogSystem::reportError(
            "Invalid mysql database log target specification, missing property user: %s",
            logTargetCfg.c_str());
        return nullptr;
    }
    const std::string& user = itr->second;

    itr = targetSpec.m_props.find("passwd");
    if (itr == targetSpec.m_props.end()) {
        ELogSystem::reportError(
            "Invalid mysql database log target specification, missing property passwd: %s",
            logTargetCfg.c_str());
        return nullptr;
    }
    const std::string& passwd = itr->second;
    return new (std::nothrow) ELogMySqlDbTarget(connString, db, user, passwd, insertQuery);
}

}  // namespace elog

#endif  // ELOG_ENABLE_MYSQL_DB_CONNECTOR
