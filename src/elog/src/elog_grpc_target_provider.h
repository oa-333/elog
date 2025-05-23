#ifndef __ELOG_GRPC_TARGET_PROVIDER_H__
#define __ELOG_GRPC_TARGET_PROVIDER_H__

#include "elog_def.h"

#ifdef ELOG_ENABLE_GRPC_CONNECTOR

#include "elog_rpc_target_provider.h"

namespace elog {

class ELogGRPCTargetProvider : public ELogRpcTargetProvider {
public:
    ELogGRPCTargetProvider() {}
    ELogGRPCTargetProvider(const ELogGRPCTargetProvider&) = delete;
    ELogGRPCTargetProvider(ELogGRPCTargetProvider&&) = delete;
    ~ELogGRPCTargetProvider() final {}

    ELogRpcTarget* loadTarget(const std::string& logTargetCfg, const ELogTargetSpec& targetSpec,
                              const std::string& server, const std::string& host, int port,
                              const std::string& functionName, const std::string& params) final;
};

}  // namespace elog

#endif  // ELOG_ENABLE_GRPC_CONNECTOR

#endif  // __ELOG_GRPC_TARGET_PROVIDER_H__