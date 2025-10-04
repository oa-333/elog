#ifndef __ELOG_REDIS_DB_TARGET_PROVIDER_H__
#define __ELOG_REDIS_DB_TARGET_PROVIDER_H__

#include "elog_def.h"

#ifdef ELOG_ENABLE_REDIS_DB_CONNECTOR

#include "db/elog_db_target_provider.h"

namespace elog {

class ELogRedisDbTargetProvider : public ELogDbTargetProvider {
public:
    ELogRedisDbTargetProvider() {}
    ELogRedisDbTargetProvider(const ELogRedisDbTargetProvider&) = delete;
    ELogRedisDbTargetProvider(ELogRedisDbTargetProvider&&) = delete;
    ELogRedisDbTargetProvider& operator=(const ELogRedisDbTargetProvider&) = delete;
    ~ELogRedisDbTargetProvider() final {}

    ELogDbTarget* loadTarget(const ELogConfigMapNode* logTargetCfg, const std::string& connString,
                             const std::string& insertQuery, ELogDbTarget::ThreadModel threadModel,
                             uint32_t maxThreads, uint64_t reconnectTimeoutMillis) final;
};

}  // namespace elog

#endif  // ELOG_ENABLE_REDIS_DB_CONNECTOR

#endif  // __ELOG_REDIS_DB_TARGET_PROVIDER_H__