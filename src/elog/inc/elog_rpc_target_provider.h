#ifndef __ELOG_RPC_TARGET_PROVIDER_H__
#define __ELOG_RPC_TARGET_PROVIDER_H__

#include "elog_rpc_target.h"
#include "elog_target_spec.h"

namespace elog {

/** @brief Parent interface for all DB log targets. */
class ELOG_API ELogRpcTargetProvider {
public:
    ELogRpcTargetProvider(const ELogRpcTargetProvider&) = delete;
    ELogRpcTargetProvider(ELogRpcTargetProvider&&) = delete;
    ELogRpcTargetProvider& operator=(const ELogRpcTargetProvider&) = delete;
    virtual ~ELogRpcTargetProvider() {}

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
    virtual ELogRpcTarget* loadTarget(const ELogConfigMapNode* logTargetCfg,
                                      const std::string& server, const std::string& host, int port,
                                      const std::string& functionName,
                                      const std::string& params) = 0;

protected:
    ELogRpcTargetProvider() {}
};

}  // namespace elog

#endif  // __ELOG_RPC_TARGET_PROVIDER_H__