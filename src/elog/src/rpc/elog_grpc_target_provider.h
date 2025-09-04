#ifndef __ELOG_GRPC_TARGET_PROVIDER_H__
#define __ELOG_GRPC_TARGET_PROVIDER_H__

#include "elog_def.h"

#ifdef ELOG_ENABLE_GRPC_CONNECTOR

#include "rpc/elog_rpc_target_provider.h"

namespace elog {

class ELogGRPCTargetProvider : public ELogRpcTargetProvider {
public:
    ELogGRPCTargetProvider();
    ELogGRPCTargetProvider(const ELogGRPCTargetProvider&) = delete;
    ELogGRPCTargetProvider(ELogGRPCTargetProvider&&) = delete;
    ELogGRPCTargetProvider& operator=(const ELogGRPCTargetProvider&) = delete;
    ~ELogGRPCTargetProvider() final;

    /**
     * @brief Loads a target from configuration.
     * @param logTargetCfg The configuration object.
     * @param server The raw server specification.
     * @param host The RPC server host name or address.
     * @param port The RPC server port.
     * @param functionName The RPC function name.
     * @param params Parameters specification given in comma separated list: ${field}, ${field}, ...
     * @return ELogRpcTarget* The resulting message queue log target, or null of failed.
     */
    ELogRpcTarget* loadTarget(const ELogConfigMapNode* logTargetCfg, const std::string& server,
                              const std::string& host, int port, const std::string& functionName,
                              const std::string& params) final;

private:
    bool loadFileProp(const ELogConfigMapNode* logTargetCfg, const char* propName,
                      const char* description, std::string& fileContents);
};

}  // namespace elog

#endif  // ELOG_ENABLE_GRPC_CONNECTOR

#endif  // __ELOG_GRPC_TARGET_PROVIDER_H__