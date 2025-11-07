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
#include "elog_schema_handler_internal.h"

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogDbSchemaHandler)

ELOG_IMPLEMENT_SCHEMA_HANDLER(ELogDbSchemaHandler)

bool ELogDbSchemaHandler::registerPredefinedProviders() {
    // register predefined providers
#ifdef ELOG_ENABLE_MYSQL_DB_CONNECTOR
    if (!initTargetProvider<ELogMySqlDbTargetProvider>(ELOG_REPORT_LOGGER, this, "mysql")) {
        return false;
    }
#endif
#ifdef ELOG_ENABLE_SQLITE_DB_CONNECTOR
    if (!initTargetProvider<ELogSQLiteDbTargetProvider>(ELOG_REPORT_LOGGER, this, "sqlite")) {
        return false;
    }
#endif
#ifdef ELOG_ENABLE_PGSQL_DB_CONNECTOR
    if (!initTargetProvider<ELogPGSQLDbTargetProvider>(ELOG_REPORT_LOGGER, this, "postgresql")) {
        return false;
    }
#endif
#ifdef ELOG_ENABLE_ORACLE_DB_CONNECTOR
    if (!initTargetProvider<ELogOracleDbTargetProvider>(ELOG_REPORT_LOGGER, this, "oracle")) {
        return false;
    }
#endif
#ifdef ELOG_ENABLE_REDIS_DB_CONNECTOR
    if (!initTargetProvider<ELogRedisDbTargetProvider>(ELOG_REPORT_LOGGER, this, "redis")) {
        return false;
    }
#endif
    return true;
}

}  // namespace elog
