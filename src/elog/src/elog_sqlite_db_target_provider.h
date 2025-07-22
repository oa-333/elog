#ifndef __ELOG_SQLITE_DB_TARGET_PROVIDER_H__
#define __ELOG_SQLITE_DB_TARGET_PROVIDER_H__

#include "elog_def.h"

#ifdef ELOG_ENABLE_SQLITE_DB_CONNECTOR

#include "elog_db_target_provider.h"

namespace elog {

class ELogSQLiteDbTargetProvider : public ELogDbTargetProvider {
public:
    ELogSQLiteDbTargetProvider() {}
    ELogSQLiteDbTargetProvider(const ELogSQLiteDbTargetProvider&) = delete;
    ELogSQLiteDbTargetProvider(ELogSQLiteDbTargetProvider&&) = delete;
    ELogSQLiteDbTargetProvider& operator=(const ELogSQLiteDbTargetProvider&) = delete;
    ~ELogSQLiteDbTargetProvider() final {}

    ELogDbTarget* loadTarget(const ELogConfigMapNode* logTargetCfg, const std::string& connString,
                             const std::string& insertQuery, ELogDbTarget::ThreadModel threadModel,
                             uint32_t maxThreads, uint64_t reconnectTimeoutMillis) final;
};

}  // namespace elog

#endif  // ELOG_ENABLE_SQLITE_DB_CONNECTOR

#endif  // __ELOG_SQLITE_DB_TARGET_PROVIDER_H__