#include "db/elog_db_target_provider.h"

#include "elog_config_loader.h"
#include "elog_report.h"

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogDbTargetProvider)

ELogTarget* ELogDbTargetProvider::loadTarget(const ELogConfigMapNode* logTargetCfg) {
    ELogDbConfig dbConfig;
    if (!loadDbAttributes(logTargetCfg, dbConfig)) {
        return nullptr;
    }
    return loadDbTarget(logTargetCfg, dbConfig);
}

bool ELogDbTargetProvider::loadDbAttributes(const ELogConfigMapNode* logTargetCfg,
                                            ELogDbConfig& dbConfig) {
    // we expect at least two properties: conn_string, insert_query, db_thread_model,
    // db_max_threads, db_reconnect_timeout
    if (!ELogConfigLoader::getLogTargetStringProperty(logTargetCfg, "database", "conn_string",
                                                      dbConfig.m_connString)) {
        return false;
    }

    if (!ELogConfigLoader::getLogTargetStringProperty(logTargetCfg, "database", "insert_query",
                                                      dbConfig.m_insertQuery)) {
        return false;
    }

    // check for optional parameters
    // optional db_thread_model
    dbConfig.m_threadModel = ELogDbThreadModel::TM_NONE;
    bool found = false;
    std::string threadModelStr;
    if (!ELogConfigLoader::getOptionalLogTargetStringProperty(
            logTargetCfg, "database", "db_thread_model", threadModelStr, &found)) {
        return false;
    }

    // parse thread model
    if (found) {
        if (threadModelStr.compare("none") == 0) {
            dbConfig.m_threadModel = ELogDbThreadModel::TM_NONE;
        } else if (threadModelStr.compare("lock") == 0) {
            dbConfig.m_threadModel = ELogDbThreadModel::TM_LOCK;
        } else if (threadModelStr.compare("conn-per-thread") == 0) {
            dbConfig.m_threadModel = ELogDbThreadModel::TM_CONN_PER_THREAD;
        } else if (threadModelStr.compare("conn-pool") == 0) {
            dbConfig.m_threadModel = ELogDbThreadModel::TM_CONN_POOL;
        } else {
            ELOG_REPORT_ERROR(
                "Invalid database log target specification, invalid thread model '%s' (context: "
                "%s)",
                threadModelStr.c_str(), logTargetCfg->getFullContext());
            return false;
        }
    }

    // check for optional db_pool_size (maximum number of threads in db thread pool)
    dbConfig.m_poolSize = ELOG_DB_DEFAULT_CONN_POOL_SIZE;
    if (!ELogConfigLoader::getOptionalLogTargetUInt32Property(
            logTargetCfg, "database", "db_pool_size", dbConfig.m_poolSize)) {
        return false;
    }

    // check for optional db_reconnect_timeout
    dbConfig.m_reconnectTimeoutMillis = ELOG_DB_RECONNECT_TIMEOUT_MILLIS;
    if (!ELogConfigLoader::getOptionalLogTargetTimeoutProperty(
            logTargetCfg, "database", "db_reconnect_timeout", dbConfig.m_reconnectTimeoutMillis,
            ELogTimeUnits::TU_MILLI_SECONDS)) {
        return false;
    }

    return true;
}

}  // namespace elog