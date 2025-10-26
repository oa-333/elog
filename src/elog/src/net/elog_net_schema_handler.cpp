#include "net/elog_net_schema_handler.h"

#ifdef ELOG_ENABLE_NET

#include <cassert>

#include "elog_common.h"
#include "elog_config_loader.h"
#include "elog_report.h"
#include "net/elog_net_target_provider.h"

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogNetSchemaHandler)

template <typename T>
static bool initNetTargetProvider(ELogNetSchemaHandler* schemaHandler, const char* name) {
    T* provider = new (std::nothrow) T(name);
    if (provider == nullptr) {
        ELOG_REPORT_ERROR("Failed to create %s network target provider, out of memory", name);
        return false;
    }
    if (!schemaHandler->registerNetTargetProvider(name, provider)) {
        ELOG_REPORT_ERROR("Failed to register %s network target provider, duplicate name", name);
        delete provider;
        return false;
    }
    return true;
}

ELogNetSchemaHandler::~ELogNetSchemaHandler() {
    for (auto& entry : m_providerMap) {
        delete entry.second;
    }
    m_providerMap.clear();
}

bool ELogNetSchemaHandler::registerPredefinedProviders() {
    // register predefined providers
    if (!initNetTargetProvider<ELogNetTargetProvider>(this, "tcp")) {
        return false;
    }
    if (!initNetTargetProvider<ELogNetTargetProvider>(this, "udp")) {
        return false;
    }
    return true;
}

bool ELogNetSchemaHandler::registerNetTargetProvider(const char* name,
                                                     ELogNetTargetProvider* provider) {
    return m_providerMap.insert(ProviderMap::value_type(name, provider)).second;
}

ELogTarget* ELogNetSchemaHandler::loadTarget(const ELogConfigMapNode* logTargetCfg) {
    std::string netType;
    if (!ELogConfigLoader::getLogTargetStringProperty(logTargetCfg, "net", "type", netType)) {
        return nullptr;
    }

    ProviderMap::iterator providerItr = m_providerMap.find(netType);
    if (providerItr != m_providerMap.end()) {
        ELogNetTargetProvider* provider = providerItr->second;
        return provider->loadTarget(logTargetCfg);
    }

    ELOG_REPORT_ERROR("Invalid network log target specification, unsupported type %s (context: %s)",
                      netType.c_str(), logTargetCfg->getFullContext());
    return nullptr;
}

void ELogNetSchemaHandler::destroy() { delete this; }

}  // namespace elog

#endif  // ELOG_ENABLE_NET
