#include "ipc/elog_ipc_schema_handler.h"

#ifdef ELOG_ENABLE_IPC

#include <cassert>

#include "elog_common.h"
#include "elog_config_loader.h"
#include "elog_report.h"
#include "ipc/elog_pipe_target_provider.h"

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogIpcSchemaHandler)

template <typename T>
static bool initIpcTargetProvider(ELogIpcSchemaHandler* schemaHandler, const char* name) {
    T* provider = new (std::nothrow) T(name);
    if (provider == nullptr) {
        ELOG_REPORT_ERROR("Failed to create %s IPC target provider, out of memory", name);
        return false;
    }
    if (!schemaHandler->registerIpcTargetProvider(name, provider)) {
        ELOG_REPORT_ERROR("Failed to register %s IPC target provider, duplicate name", name);
        delete provider;
        return false;
    }
    return true;
}

ELogIpcSchemaHandler::~ELogIpcSchemaHandler() {
    for (auto& entry : m_providerMap) {
        delete entry.second;
    }
    m_providerMap.clear();
}

bool ELogIpcSchemaHandler::registerPredefinedProviders() {
    // register predefined providers
    if (!initIpcTargetProvider<ELogPipeTargetProvider>(this, "pipe")) {
        return false;
    }
    return true;
}

bool ELogIpcSchemaHandler::registerIpcTargetProvider(const char* name,
                                                     ELogIpcTargetProvider* provider) {
    return m_providerMap.insert(ProviderMap::value_type(name, provider)).second;
}

ELogTarget* ELogIpcSchemaHandler::loadTarget(const ELogConfigMapNode* logTargetCfg) {
    std::string ipcType;
    if (!ELogConfigLoader::getLogTargetStringProperty(logTargetCfg, "ipc", "type", ipcType)) {
        return nullptr;
    }

    ProviderMap::iterator providerItr = m_providerMap.find(ipcType);
    if (providerItr != m_providerMap.end()) {
        ELogIpcTargetProvider* provider = providerItr->second;
        return provider->loadTarget(logTargetCfg);
    }

    ELOG_REPORT_ERROR("Invalid IPC log target specification, unsupported type %s (context: %s)",
                      ipcType.c_str(), logTargetCfg->getFullContext());
    return nullptr;
}

void ELogIpcSchemaHandler::destroy() { delete this; }

}  // namespace elog

#endif  // ELOG_ENABLE_NET
