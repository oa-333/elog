#include "elog_db_schema_handler.h"

#include <cassert>

#include "elog_common.h"
#include "elog_config_loader.h"
#include "elog_error.h"
#include "elog_mysql_db_target_provider.h"
#include "elog_pgsql_db_target_provider.h"
#include "elog_sqlite_db_target_provider.h"

namespace elog {

template <typename T>
static bool initDbTargetProvider(ELogDbSchemaHandler* schemaHandler, const char* name) {
    T* provider = new (std::nothrow) T();
    if (provider == nullptr) {
        ELOG_REPORT_ERROR("Failed to create %s db target provider, out of memory", name);
        return false;
    }
    if (!schemaHandler->registerDbTargetProvider(name, provider)) {
        ELOG_REPORT_ERROR("Failed to register %s db target provider, duplicate name", name);
        delete provider;
        return false;
    }
    return true;
}

ELogDbSchemaHandler::~ELogDbSchemaHandler() {
    for (auto& entry : m_providerMap) {
        delete entry.second;
    }
    m_providerMap.clear();
}

bool ELogDbSchemaHandler::registerPredefinedProviders() {
    // register predefined providers
#ifdef ELOG_ENABLE_MYSQL_DB_CONNECTOR
    if (!initDbTargetProvider<ELogMySqlDbTargetProvider>(this, "mysql")) {
        return false;
    }
#endif
#ifdef ELOG_ENABLE_SQLITE_DB_CONNECTOR
    if (!initDbTargetProvider<ELogSQLiteDbTargetProvider>(this, "sqlite")) {
        return false;
    }
#endif
#ifdef ELOG_ENABLE_PGSQL_DB_CONNECTOR
    if (!initDbTargetProvider<ELogPGSQLDbTargetProvider>(this, "postgresql")) {
        return false;
    }
#endif
#ifdef ELOG_ENABLE_ORACLE_DB_CONNECTOR
    if (!initDbTargetProvider<ELogOracleDbTargetProvider>(this, "oracle")) {
        return false;
    }
#endif
#ifdef ELOG_ENABLE_SQLSERVER_DB_CONNECTOR
    if (!initDbTargetProvider<ELogSqlServerDbTargetProvider>(this, "sqlserver")) {
        return false;
    }
#endif
    return true;
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

    // in addition, we expect at least two properties: conn_string, insert_query, db_thread_model,
    // db_max_threads, db_reconnect_timeout_millis
    if (targetSpec.m_props.size() < 2) {
        ELOG_REPORT_ERROR(
            "Invalid database log target specification, expected at least two properties: %s",
            logTargetCfg.c_str());
        return nullptr;
    }

    ELogPropertyMap::const_iterator itr = targetSpec.m_props.find("conn_string");
    if (itr == targetSpec.m_props.end()) {
        ELOG_REPORT_ERROR(
            "Invalid database log target specification, missing property conn_string: %s",
            logTargetCfg.c_str());
        return nullptr;
    }
    const std::string& connString = itr->second;

    itr = targetSpec.m_props.find("insert_query");
    if (itr == targetSpec.m_props.end()) {
        ELOG_REPORT_ERROR(
            "Invalid database log target specification, missing property insert_query: %s",
            logTargetCfg.c_str());
        return nullptr;
    }
    const std::string& insertQuery = itr->second;

    // check for optional parameters
    // optional db_thread_model
    ELogDbTarget::ThreadModel threadModel = ELogDbTarget::ThreadModel::TM_NONE;
    itr = targetSpec.m_props.find("db_thread_model");
    if (itr != targetSpec.m_props.end()) {
        const std::string& threadModelStr = itr->second;
        if (threadModelStr.compare("none") == 0) {
            threadModel = ELogDbTarget::ThreadModel::TM_NONE;
        } else if (threadModelStr.compare("lock") == 0) {
            threadModel = ELogDbTarget::ThreadModel::TM_LOCK;
        } else if (threadModelStr.compare("conn-per-thread") == 0) {
            threadModel = ELogDbTarget::ThreadModel::TM_CONN_PER_THREAD;
        } else {
            ELOG_REPORT_ERROR(
                "Invalid database log target specification, invalid thread model '%s': %s",
                threadModelStr.c_str(), logTargetCfg.c_str());
            return nullptr;
        }
    }

    // check for optional db_max_threads
    uint32_t maxThreads = ELOG_DB_MAX_THREADS;
    itr = targetSpec.m_props.find("db_max_threads");
    if (itr != targetSpec.m_props.end()) {
        const std::string& maxThreadsStr = itr->second;
        if (!parseIntProp("db_max_threads", logTargetCfg, maxThreadsStr, maxThreads, true)) {
            ELOG_REPORT_ERROR(
                "Invalid database log target specification, invalid maximum thread count '%s': %s",
                maxThreadsStr.c_str(), logTargetCfg.c_str());
            return nullptr;
        }
    }

    // check for optional db_reconnect_timeout_millis
    uint32_t reconnectTimeoutMillis = ELOG_DB_RECONNECT_TIMEOUT_MILLIS;
    itr = targetSpec.m_props.find("db_reconnect_timeout_millis");
    if (itr != targetSpec.m_props.end()) {
        const std::string& reconnectTimeoutMillisStr = itr->second;
        if (!parseIntProp("db_reconnect_timeout_millis", logTargetCfg, reconnectTimeoutMillisStr,
                          reconnectTimeoutMillis, true)) {
            ELOG_REPORT_ERROR(
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

    ELOG_REPORT_ERROR("Invalid database log target specification, unsupported db type %s: %s",
                      dbType.c_str(), logTargetCfg.c_str());
    return nullptr;
}

ELogTarget* ELogDbSchemaHandler::loadTarget(const std::string& logTargetCfg,
                                            const ELogTargetNestedSpec& targetNestedSpec) {
    // first make sure there ar no log target sub-specs
    if (targetNestedSpec.m_subSpec.find("log_target") != targetNestedSpec.m_subSpec.end()) {
        ELOG_REPORT_ERROR("Database log target cannot have sub-targets: %s", logTargetCfg.c_str());
        return nullptr;
    }

    // no difference, just call URL style loading
    return loadTarget(logTargetCfg, targetNestedSpec.m_spec);
}

ELogTarget* ELogDbSchemaHandler::loadTarget(const ELogConfigMapNode* logTargetCfg) {
    // the path represents the db-type
    // current predefined types are supported:
    // mysql
    // sqlite
    // postgresql
    // oracle
    // sqlserver

    // get mandatory properties
    std::string dbType;
    if (!ELogConfigLoader::getLogTargetStringProperty(logTargetCfg, "database", "path", dbType)) {
        return nullptr;
    }

    // in addition, we expect at least two properties: conn_string, insert_query, db_thread_model,
    // db_max_threads, db_reconnect_timeout_millis
    std::string connString;
    if (!ELogConfigLoader::getLogTargetStringProperty(logTargetCfg, "database", "conn_string",
                                                      connString)) {
        return nullptr;
    }

    std::string insertQuery;
    if (!ELogConfigLoader::getLogTargetStringProperty(logTargetCfg, "database", "insert_query",
                                                      insertQuery)) {
        return nullptr;
    }

    // check for optional parameters
    // optional db_thread_model
    ELogDbTarget::ThreadModel threadModel = ELogDbTarget::ThreadModel::TM_NONE;
    bool found = false;
    std::string threadModelStr;
    if (!ELogConfigLoader::getOptionalLogTargetStringProperty(
            logTargetCfg, "database", "db_thread_model", threadModelStr, &found)) {
        return nullptr;
    }

    if (found) {
        if (threadModelStr.compare("none") == 0) {
            threadModel = ELogDbTarget::ThreadModel::TM_NONE;
        } else if (threadModelStr.compare("lock") == 0) {
            threadModel = ELogDbTarget::ThreadModel::TM_LOCK;
        } else if (threadModelStr.compare("conn-per-thread") == 0) {
            threadModel = ELogDbTarget::ThreadModel::TM_CONN_PER_THREAD;
        } else {
            ELOG_REPORT_ERROR(
                "Invalid database log target specification, invalid thread model '%s' (context: "
                "%s)",
                threadModelStr.c_str(), logTargetCfg->getFullContext());
            return nullptr;
        }
    }

    // check for optional db_max_threads
    int64_t maxThreads = ELOG_DB_MAX_THREADS;
    if (!ELogConfigLoader::getOptionalLogTargetIntProperty(logTargetCfg, "database",
                                                           "db_max_threads", maxThreads)) {
        return nullptr;
    }

    // check for optional db_reconnect_timeout_millis
    int64_t reconnectTimeoutMillis = ELOG_DB_RECONNECT_TIMEOUT_MILLIS;
    if (!ELogConfigLoader::getOptionalLogTargetIntProperty(
            logTargetCfg, "database", "db_reconnect_timeout_millis", reconnectTimeoutMillis)) {
        return nullptr;
    }

    ProviderMap::iterator providerItr = m_providerMap.find(dbType);
    if (providerItr != m_providerMap.end()) {
        ELogDbTargetProvider* provider = providerItr->second;
        return provider->loadTarget(logTargetCfg, connString, insertQuery, threadModel, maxThreads,
                                    reconnectTimeoutMillis);
    }

    ELOG_REPORT_ERROR(
        "Invalid database log target specification, unsupported db type %s (context: %s)",
        dbType.c_str(), logTargetCfg->getFullContext());
    return nullptr;
}

}  // namespace elog
