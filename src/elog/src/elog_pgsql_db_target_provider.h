#ifndef __ELOG_PGSQL_DB_TARGET_PROVIDER_H__
#define __ELOG_PGSQL_DB_TARGET_PROVIDER_H__

#include "elog_def.h"

#ifdef ELOG_ENABLE_PGSQL_DB_CONNECTOR

#include "elog_common.h"
#include "elog_db_target_provider.h"

namespace elog {

class ELogPGSQLDbTargetProvider : public ELogDbTargetProvider {
public:
    ELogPGSQLDbTargetProvider() {}
    ELogPGSQLDbTargetProvider(const ELogPGSQLDbTargetProvider&) = delete;
    ELogPGSQLDbTargetProvider(ELogPGSQLDbTargetProvider&&) = delete;
    ~ELogPGSQLDbTargetProvider() final {}

    ELogDbTarget* loadTarget(const std::string& logTargetCfg, const ELogTargetSpec& targetSpec,
                             const std::string& connString, const std::string& insertQuery) final;
};

}  // namespace elog

#endif  // ELOG_ENABLE_PGSQL_DB_CONNECTOR

#endif  // __ELOG_PGSQL_DB_TARGET_PROVIDER_H__