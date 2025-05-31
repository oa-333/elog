#include "elog_mon_schema_handler.h"

#include <cassert>

#include "elog_common.h"
#include "elog_error.h"
#ifdef ELOG_ENABLE_GRAFANA_CONNECTOR
#include "elog_grafana_target_provider.h"
#endif

namespace elog {

template <typename T>
static bool initMonTargetProvider(ELogMonSchemaHandler* schemaHandler, const char* name) {
    T* provider = new (std::nothrow) T();
    if (provider == nullptr) {
        ELOG_REPORT_ERROR("Failed to create %s monitoring tool target provider, out of memory",
                          name);
        return false;
    }
    if (!schemaHandler->registerMonTargetProvider(name, provider)) {
        ELOG_REPORT_ERROR("Failed to register %s monitoring tool target provider, duplicate name",
                          name);
        delete provider;
        return false;
    }
    return true;
}

ELogMonSchemaHandler::~ELogMonSchemaHandler() {
    for (auto& entry : m_providerMap) {
        delete entry.second;
    }
    m_providerMap.clear();
}

bool ELogMonSchemaHandler::registerPredefinedProviders() {
    // register predefined providers
#ifdef ELOG_ENABLE_GRAFANA_CONNECTOR
    if (!initMonTargetProvider<ELogGrafanaTargetProvider>(this, "grafana")) {
        return false;
    }
#endif
    return true;
}

bool ELogMonSchemaHandler::registerMonTargetProvider(const char* monitorName,
                                                     ELogMonTargetProvider* provider) {
    return m_providerMap.insert(ProviderMap::value_type(monitorName, provider)).second;
}

ELogTarget* ELogMonSchemaHandler::loadTarget(const std::string& logTargetCfg,
                                             const ELogTargetSpec& targetSpec) {
    // the path represents the monitoring tool type
    // current predefined types are supported:
    // grafana_loki
    const std::string& monType = targetSpec.m_path;

    ProviderMap::iterator providerItr = m_providerMap.find(monType);
    if (providerItr != m_providerMap.end()) {
        ELogMonTargetProvider* provider = providerItr->second;
        return provider->loadTarget(logTargetCfg, targetSpec);
    }

    ELOG_REPORT_ERROR("Invalid monitoring tool log target specification, unsupported type %s: %s",
                      monType.c_str(), logTargetCfg.c_str());
    return nullptr;
}

ELogTarget* ELogMonSchemaHandler::loadTarget(const std::string& logTargetCfg,
                                             const ELogTargetNestedSpec& targetNestedSpec) {
    // first make sure there ar no log target sub-specs
    if (targetNestedSpec.m_subSpec.find("log_target") != targetNestedSpec.m_subSpec.end()) {
        ELOG_REPORT_ERROR("Monitoring tool log target cannot have sub-targets: %s",
                          logTargetCfg.c_str());
        return nullptr;
    }

    // no difference, just call URL style loading
    return loadTarget(logTargetCfg, targetNestedSpec.m_spec);
}

}  // namespace elog
