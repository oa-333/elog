#ifndef __ELOG_PGSQL_DB_TARGET_PROVIDER_H__
#define __ELOG_PGSQL_DB_TARGET_PROVIDER_H__

#include "elog_def.h"

#ifdef ELOG_ENABLE_PGSQL_DB_CONNECTOR

#include "db/elog_db_target_provider.h"

namespace elog {

class ELogPGSQLDbTargetProvider : public ELogDbTargetProvider {
public:
    ELogPGSQLDbTargetProvider() {}
    ELogPGSQLDbTargetProvider(const ELogPGSQLDbTargetProvider&) = delete;
    ELogPGSQLDbTargetProvider(ELogPGSQLDbTargetProvider&&) = delete;
    ELogPGSQLDbTargetProvider& operator=(const ELogPGSQLDbTargetProvider&) = delete;
    ~ELogPGSQLDbTargetProvider() final {}

protected:
    ELogTarget* loadDbTarget(const ELogConfigMapNode* logTargetCfg,
                             const ELogDbConfig& dbConfig) final;
};

}  // namespace elog

#endif  // ELOG_ENABLE_PGSQL_DB_CONNECTOR

#endif  // __ELOG_PGSQL_DB_TARGET_PROVIDER_H__