#include "elog_sqlite_db_target_provider.h"

#ifdef ELOG_ENABLE_SQLITE_DB_CONNECTOR

#include "elog_error.h"
#include "elog_sqlite_db_target.h"

namespace elog {

ELogDbTarget* ELogSQLiteDbTargetProvider::loadTarget(
    const std::string& logTargetCfg, const ELogTargetSpec& targetSpec,
    const std::string& connString, const std::string& insertQuery,
    ELogDbTarget::ThreadModel threadModel, uint32_t maxThreads, uint32_t reconnectTimeoutMillis) {
    ELogDbTarget* target = new (std::nothrow) ELogSQLiteDbTarget(
        connString, insertQuery, threadModel, maxThreads, reconnectTimeoutMillis);
    if (target == nullptr) {
        ELOG_REPORT_ERROR("Failed to allocate SQLite log target, out of memory");
    }
    return target;
}

}  // namespace elog

#endif  // ELOG_ENABLE_SQLITE_DB_CONNECTOR
