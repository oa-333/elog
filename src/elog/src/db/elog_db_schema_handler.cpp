#include "db/elog_db_schema_handler.h"

#include <cassert>

#include "db/elog_mysql_db_target_provider.h"
#include "db/elog_pgsql_db_target_provider.h"
#include "db/elog_redis_db_target_provider.h"
#include "db/elog_sqlite_db_target_provider.h"
#include "elog_common.h"
#include "elog_config_loader.h"
#include "elog_internal.h"
#include "elog_report.h"

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogDbSchemaHandler)

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
#ifdef ELOG_ENABLE_REDIS_DB_CONNECTOR
    if (!initDbTargetProvider<ELogRedisDbTargetProvider>(this, "redis")) {
        return false;
    }
#endif
    return true;
}

bool ELogDbSchemaHandler::registerDbTargetProvider(const char* dbName,
                                                   ELogDbTargetProvider* provider) {
    return m_providerMap.insert(ProviderMap::value_type(dbName, provider)).second;
}

ELogTarget* ELogDbSchemaHandler::loadTarget(const ELogConfigMapNode* logTargetCfg) {
    // the path represents the db-type
    // current predefined types are supported:
    // mysql
    // sqlite
    // postgresql
    // redis

    // get mandatory properties
    std::string dbType;
    if (!ELogConfigLoader::getLogTargetStringProperty(logTargetCfg, "database", "type", dbType)) {
        return nullptr;
    }

    // in addition, we expect at least two properties: conn_string, insert_query, db_thread_model,
    // db_max_threads, db_reconnect_timeout
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
        } else if (threadModelStr.compare("conn-pool") == 0) {
            threadModel = ELogDbTarget::ThreadModel::TM_CONN_POOL;
        } else {
            ELOG_REPORT_ERROR(
                "Invalid database log target specification, invalid thread model '%s' (context: "
                "%s)",
                threadModelStr.c_str(), logTargetCfg->getFullContext());
            return nullptr;
        }
    }

    // check for optional db_poll_size
    uint32_t poolSize = ELOG_DB_DEFAULT_CONN_POOL_SIZE;
    if (!ELogConfigLoader::getOptionalLogTargetUInt32Property(logTargetCfg, "database",
                                                              "db_pool_size", poolSize)) {
        return nullptr;
    }

    // check for optional db_reconnect_timeout
    uint64_t reconnectTimeoutMillis = ELOG_DB_RECONNECT_TIMEOUT_MILLIS;
    if (!ELogConfigLoader::getOptionalLogTargetTimeoutProperty(
            logTargetCfg, "database", "db_reconnect_timeout", reconnectTimeoutMillis,
            ELogTimeUnits::TU_MILLI_SECONDS)) {
        return nullptr;
    }

    ProviderMap::iterator providerItr = m_providerMap.find(dbType);
    if (providerItr != m_providerMap.end()) {
        ELogDbTargetProvider* provider = providerItr->second;
        return provider->loadTarget(logTargetCfg, connString, insertQuery, threadModel, poolSize,
                                    reconnectTimeoutMillis);
    }

    ELOG_REPORT_ERROR(
        "Invalid database log target specification, unsupported db type %s (context: %s)",
        dbType.c_str(), logTargetCfg->getFullContext());
    return nullptr;
}

}  // namespace elog
