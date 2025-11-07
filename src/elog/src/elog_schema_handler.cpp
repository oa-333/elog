#include "elog_schema_handler.h"

#include "elog_config_loader.h"
#include "elog_report.h"

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogSchemaHandler)

bool ELogSchemaHandler::registerTargetProvider(const char* typeName, ELogTargetProvider* provider) {
    bool res = m_providerMap.insert(ProviderMap::value_type(typeName, provider)).second;
    if (!res) {
        ELOG_REPORT_ERROR(
            "Cannot add load target provider with type name %S to schema handler %s: already "
            "exists",
            typeName, getSchemeName());
    }
    return res;
}

ELogTarget* ELogSchemaHandler::loadTarget(const ELogConfigMapNode* logTargetCfg) {
    std::string typeName;
    if (!ELogConfigLoader::getLogTargetStringProperty(logTargetCfg, getSchemeName(), "type",
                                                      typeName)) {
        return nullptr;
    }

    // get the provider and create the target
    ProviderMap::iterator providerItr = m_providerMap.find(typeName);
    if (providerItr != m_providerMap.end()) {
        ELogTargetProvider* provider = providerItr->second;
        return provider->loadTarget(logTargetCfg);
    }

    ELOG_REPORT_ERROR("Invalid %s log target specification, unsupported type %s (context: %s)",
                      getSchemeName(), typeName.c_str(), logTargetCfg->getFullContext());
    return nullptr;
}

ELogSchemaHandler::~ELogSchemaHandler() {
    for (auto& entry : m_providerMap) {
        delete entry.second;
    }
    m_providerMap.clear();
}

}  // namespace elog
