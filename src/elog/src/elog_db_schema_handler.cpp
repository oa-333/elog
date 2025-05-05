#include "elog_db_schema_handler.h"

#include <algorithm>

#include "elog_mysql_db_handler.h"
#include "elog_system.h"

namespace elog {

ELogTarget* ELogDbSchemaHandler::loadTarget(const std::string& logTargetCfg,
                                            const ELogTargetSpec& targetSpec) {
    // the path represents the db-type
    // current predefined types are supported:
    // mysql
    // sqlite
    // postgresql
    // oracle
    // sqlserver
    const std::string& dbType = targetSpec.m_path;

    // in addition, we expect at least two properties: conn-string and insert-query
    if (targetSpec.m_props.size() < 2) {
        ELogSystem::reportError(
            "Invalid database log target specification, expected at least two properties: %s",
            logTargetCfg.c_str());
        return nullptr;
    }

    ELogPropertyMap::const_iterator itr = targetSpec.m_props.find("conn-string");
    if (itr == targetSpec.m_props.end()) {
        ELogSystem::reportError(
            "Invalid database log target specification, missing property conn-string: %s",
            logTargetCfg.c_str());
        return nullptr;
    }
    const std::string& connString = itr->second;

    itr = targetSpec.m_props.find("insert-query");
    if (itr == targetSpec.m_props.end()) {
        ELogSystem::reportError(
            "Invalid database log target specification, missing property insert-query: %s",
            logTargetCfg.c_str());
        return nullptr;
    }
    const std::string& insertQuery = itr->second;

#ifdef ELOG_ENABLE_MYSQL_DB_CONNECTOR
    if (dbType.compare("mysql") == 0) {
        return ELogMySqlDbHandler::loadTarget(logTargetCfg, targetSpec, connString, insertQuery);
    }
#endif
#ifdef ELOG_ENABLE_SQLITE_DB_CONNECTOR
    if (dbType.compare("sqlite") == 0) {
        return ELogSQLiteDbHandler::loadTarget(logTargetCfg, targetSpec, connString, insertQuery);
    }
#endif
#ifdef ELOG_ENABLE_POSTGRESQL_DB_CONNECTOR
    if (dbType.compare("postgresql") == 0) {
        return ELogPostgreSqlDblHandler::loadTarget(logTargetCfg, targetSpec, connString,
                                                    insertQuery);
    }
#endif
#ifdef ELOG_ENABLE_ORACLE_DB_CONNECTOR
    if (dbType.compare("oracle") == 0) {
        return ELogOracleDbHandler::loadTarget(logTargetCfg, targetSpec, connString, insertQuery);
    }
#endif
#ifdef ELOG_ENABLE_SQLSERVER_DB_CONNECTOR
    if (dbType.compare("sqlserver") == 0) {
        return ELogSqlServerDbHandler::loadTarget(logTargetCfg, targetSpec, connString,
                                                  insertQuery);
    }
#endif

    ELogSystem::reportError("Invalid database log target specification, unsupported db type %s: %s",
                            dbType.c_str(), logTargetCfg.c_str());
    return nullptr;
}

}  // namespace elog
