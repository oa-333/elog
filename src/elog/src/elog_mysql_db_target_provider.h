#ifndef __ELOG_MYSQL_DB_TARGET_PROVIDER_H__
#define __ELOG_MYSQL_DB_TARGET_PROVIDER_H__

#include "elog_def.h"

#ifdef ELOG_MSVC
#define ELOG_ENABLE_MYSQL_DB_CONNECTOR
#endif

#ifdef ELOG_ENABLE_MYSQL_DB_CONNECTOR

#include "elog_common.h"
#include "elog_db_target_provider.h"

namespace elog {

class ELogMySqlDbTargetProvider : public ELogDbTargetProvider {
public:
    ELogMySqlDbTargetProvider() {}
    ELogMySqlDbTargetProvider(const ELogMySqlDbTargetProvider&) = delete;
    ELogMySqlDbTargetProvider(ELogMySqlDbTargetProvider&&) = delete;
    ~ELogMySqlDbTargetProvider() final {}

    ELogDbTarget* loadTarget(const std::string& logTargetCfg, const ELogTargetSpec& targetSpec,
                             const std::string& connString, const std::string& insertQuery) final;
};

}  // namespace elog

#endif  // ELOG_ENABLE_MYSQL_DB_CONNECTOR

#endif  // __ELOG_MYSQL_DB_TARGET_PROVIDER_H__