#include "elog_db_schema_handler.h"

#include <cassert>

#include "elog_mysql_db_target_provider.h"
#include "elog_pgsql_db_target_provider.h"
#include "elog_sqlite_db_target_provider.h"
#include "elog_system.h"

namespace elog {

template <typename T>
static bool initDbTargetProvider(ELogDbSchemaHandler* schemaHandler, const char* name) {
    T* provider = new (std::nothrow) T();
    if (provider == nullptr) {
        ELogSystem::reportError("Failed to create %s db target provider, out of memory", name);
        return false;
    }
    if (!schemaHandler->registerDbTargetProvider(name, provider)) {
        ELogSystem::reportError("Failed to register %s db target provider, duplicate name", name);
        delete provider;
        return false;
    }
    return true;
}

ELogDbSchemaHandler::ELogDbSchemaHandler() {
    // register predefined providers
#ifdef ELOG_ENABLE_MYSQL_DB_CONNECTOR
    if (!initDbTargetProvider<ELogMySqlDbTargetProvider>(this, "mysql")) {
        assert(false);
    }
#endif
#ifdef ELOG_ENABLE_SQLITE_DB_CONNECTOR
    if (!initDbTargetProvider<ELogSQLiteDbTargetProvider>(this, "sqlite")) {
        assert(false);
    }
#endif
#ifdef ELOG_ENABLE_PGSQL_DB_CONNECTOR
    if (!initDbTargetProvider<ELogPGSQLDbTargetProvider>(this, "postgresql")) {
        assert(false);
    }
#endif
#ifdef ELOG_ENABLE_ORACLE_DB_CONNECTOR
    if (!initDbTargetProvider<ELogOracleDbTargetProvider>(this, "oracle")) {
        assert(false);
    }
#endif
#ifdef ELOG_ENABLE_SQLSERVER_DB_CONNECTOR
    if (!initDbTargetProvider<ELogSqlServerDbTargetProvider>(this, "sqlserver")) {
        assert(false);
    }
#endif
}

ELogDbSchemaHandler::~ELogDbSchemaHandler() {
    // cleanup provider map
}

bool ELogDbSchemaHandler::registerDbTargetProvider(const char* dbName,
                                                   ELogDbTargetProvider* provider) {
    return m_providerMap.insert(ProviderMap::value_type(dbName, provider)).second;
}

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

    // in addition, we expect at least two properties: conn-string, insert-query, db-thread-model,
    // db-max-threads, db-reconnect-timeout-millis
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

    // check for optional parameters
    // optional db-thread-model
    ELogDbTarget::ThreadModel threadModel = ELogDbTarget::ThreadModel::TM_NONE;
    itr = targetSpec.m_props.find("db-thread-model");
    if (itr != targetSpec.m_props.end()) {
        const std::string& threadModelStr = itr->second;
        if (threadModelStr.compare("none") == 0) {
            threadModel = ELogDbTarget::ThreadModel::TM_NONE;
        } else if (threadModelStr.compare("lock") == 0) {
            threadModel = ELogDbTarget::ThreadModel::TM_LOCK;
        } else if (threadModelStr.compare("conn-per-thread") == 0) {
            threadModel = ELogDbTarget::ThreadModel::TM_CONN_PER_THREAD;
        } else {
            ELogSystem::reportError(
                "Invalid database log target specification, invalid thread model '%s': %s",
                threadModelStr.c_str(), logTargetCfg.c_str());
            return nullptr;
        }
    }

    // check for optional db-max-threads
    uint32_t maxThreads = ELOG_DB_MAX_THREADS;
    itr = targetSpec.m_props.find("db-max-threads");
    if (itr != targetSpec.m_props.end()) {
        const std::string& maxThreadsStr = itr->second;
        if (!parseIntProp("db-max-threads", logTargetCfg, maxThreadsStr, maxThreads, true)) {
            ELogSystem::reportError(
                "Invalid database log target specification, invalid maximum thread count '%s': %s",
                maxThreadsStr.c_str(), logTargetCfg.c_str());
            return nullptr;
        }
    }

    // check for optional db-reconnect-timeout-millis
    uint32_t reconnectTimeoutMillis = ELOG_DB_RECONNECT_TIMEOUT_MILLIS;
    itr = targetSpec.m_props.find("db-reconnect-timeout-millis");
    if (itr != targetSpec.m_props.end()) {
        const std::string& reconnectTimeoutMillisStr = itr->second;
        if (!parseIntProp("db-reconnect-timeout-millis", logTargetCfg, reconnectTimeoutMillisStr,
                          reconnectTimeoutMillis, true)) {
            ELogSystem::reportError(
                "Invalid database log target specification, invalid reconnect timeout value '%s': "
                "%s",
                reconnectTimeoutMillisStr.c_str(), logTargetCfg.c_str());
            return nullptr;
        }
    }

    ProviderMap::iterator providerItr = m_providerMap.find(dbType);
    if (providerItr != m_providerMap.end()) {
        ELogDbTargetProvider* provider = providerItr->second;
        return provider->loadTarget(logTargetCfg, targetSpec, connString, insertQuery, threadModel,
                                    maxThreads, reconnectTimeoutMillis);
    }

    ELogSystem::reportError("Invalid database log target specification, unsupported db type %s: %s",
                            dbType.c_str(), logTargetCfg.c_str());
    return nullptr;
}

}  // namespace elog
