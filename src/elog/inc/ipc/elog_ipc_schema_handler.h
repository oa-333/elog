#ifndef __ELOG_IPC_SCHEMA_HANDLER_H__
#define __ELOG_IPC_SCHEMA_HANDLER_H__

#ifdef ELOG_ENABLE_IPC

#include <unordered_map>

#include "elog_schema_handler.h"
#include "ipc/elog_ipc_target_provider.h"

namespace elog {

/** @brief Handler for loading IPC log target from configuration. */
class ELOG_API ELogIpcSchemaHandler : public ELogSchemaHandler {
public:
    ELogIpcSchemaHandler() {}
    ELogIpcSchemaHandler(const ELogIpcSchemaHandler&) = delete;
    ELogIpcSchemaHandler(ELogIpcSchemaHandler&&) = delete;
    ELogIpcSchemaHandler& operator=(const ELogIpcSchemaHandler&) = delete;

    /** @brief Destructor. */
    ~ELogIpcSchemaHandler() final;

    /** @brief Registers predefined target providers. */
    bool registerPredefinedProviders() final;

    /** @brief Register external net target provider. */
    bool registerIpcTargetProvider(const char* name, ELogIpcTargetProvider* provider);

    /**
     * @brief Loads a log target from a configuration object.
     * @param logTargetCfg The log target configuration object.
     * @return ELogTarget* The resulting log target or null if failed.
     */
    ELogTarget* loadTarget(const ELogConfigMapNode* logTargetCfg) final;

private:
    typedef std::unordered_map<std::string, ELogIpcTargetProvider*> ProviderMap;
    ProviderMap m_providerMap;
};

}  // namespace elog

#endif  // ELOG_ENABLE_IPC

#endif  // __ELOG_IPC_SCHEMA_HANDLER_H__