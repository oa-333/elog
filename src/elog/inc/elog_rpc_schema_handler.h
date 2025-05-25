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
    ELogRpcSchemaHandler(const ELogRpcSchemaHandler&) = default;
    ELogRpcSchemaHandler(ELogRpcSchemaHandler&&) = default;

    /** @brief Destructor. */
    ~ELogRpcSchemaHandler() final;

    /** @brief Registers predefined target providers. */
    bool registerPredefinedProviders() final;

    /** @brief Register external RPC log target provider. */
    bool registerRpcTargetProvider(const char* providerName, ELogRpcTargetProvider* provider);

    /**
     * @brief Loads a log target by a specification.
     * @param logTargetCfg The log target string specification.
     * @param targetSpec The log target specification.
     * @return ELogTarget* The resulting log target or null if failed.
     */
    ELogTarget* loadTarget(const std::string& logTargetCfg, const ELogTargetSpec& targetSpec) final;

    /**
     * @brief Loads a log target by a specification.
     * @param logTargetCfg The log target string specification.
     * @param targetNestedSpec The log target nested specification.
     * @return ELogTarget* The resulting log target or null if failed.
     */
    ELogTarget* loadTarget(const std::string& logTargetCfg,
                           const ELogTargetNestedSpec& targetNestedSpec) final;

private:
    typedef std::unordered_map<std::string, ELogRpcTargetProvider*> ProviderMap;
    ProviderMap m_providerMap;
};

}  // namespace elog

#endif  // __ELOG_RPC_SCHEMA_HANDLER_H__