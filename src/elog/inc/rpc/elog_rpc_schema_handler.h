#ifndef __ELOG_RPC_SCHEMA_HANDLER_H__
#define __ELOG_RPC_SCHEMA_HANDLER_H__

#include <unordered_map>

#include "elog_rpc_target_provider.h"
#include "elog_schema_handler.h"

namespace elog {

/** @brief Handler for loading RPC log target from configuration. */
class ELOG_API ELogRpcSchemaHandler : public ELogSchemaHandler {
public:
    ELogRpcSchemaHandler() {}
    ELogRpcSchemaHandler(const ELogRpcSchemaHandler&) = delete;
    ELogRpcSchemaHandler(ELogRpcSchemaHandler&&) = delete;
    ELogRpcSchemaHandler& operator=(const ELogRpcSchemaHandler&) = delete;

    /** @brief Destructor. */
    ~ELogRpcSchemaHandler() final;

    /** @brief Registers predefined target providers. */
    bool registerPredefinedProviders() final;

    /** @brief Register external RPC log target provider. */
    bool registerRpcTargetProvider(const char* providerName, ELogRpcTargetProvider* provider);

    /**
     * @brief Loads a log target from a configuration object.
     * @param logTargetCfg The log target configuration object.
     * @return ELogTarget* The resulting log target or null if failed.
     */
    ELogTarget* loadTarget(const ELogConfigMapNode* logTargetCfg) final;

private:
    typedef std::unordered_map<std::string, ELogRpcTargetProvider*> ProviderMap;
    ProviderMap m_providerMap;
};

}  // namespace elog

#endif  // __ELOG_RPC_SCHEMA_HANDLER_H__