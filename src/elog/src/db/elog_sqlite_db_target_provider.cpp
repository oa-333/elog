#include "db/elog_sqlite_db_target_provider.h"

#ifdef ELOG_ENABLE_SQLITE_DB_CONNECTOR

#include "db/elog_sqlite_db_target.h"
#include "elog_report.h"

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogSQLiteDbTargetProvider)

ELogTarget* ELogSQLiteDbTargetProvider::loadDbTarget(const ELogConfigMapNode* logTargetCfg,
                                                     const ELogDbConfig& dbConfig) {
    ELogDbTarget* target = new (std::nothrow) ELogSQLiteDbTarget(dbConfig);
    if (target == nullptr) {
        ELOG_REPORT_ERROR("Failed to allocate SQLite log target, out of memory");
    }
    return target;
}

}  // namespace elog

#endif  // ELOG_ENABLE_SQLITE_DB_CONNECTOR
