#include "elog_mon_schema_handler.h"

#include <cassert>

#include "elog_common.h"
#include "elog_config_loader.h"
#include "elog_report.h"

#ifdef ELOG_ENABLE_GRAFANA_CONNECTOR
#include "elog_grafana_target_provider.h"
#endif

#ifdef ELOG_ENABLE_SENTRY_CONNECTOR
#include "elog_sentry_target_provider.h"
#endif

#ifdef ELOG_ENABLE_DATADOG_CONNECTOR
#include "elog_datadog_target_provider.h"
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
#ifdef ELOG_ENABLE_SENTRY_CONNECTOR
    if (!initMonTargetProvider<ELogSentryTargetProvider>(this, "sentry")) {
        return false;
    }
#endif
#ifdef ELOG_ENABLE_DATADOG_CONNECTOR
    if (!initMonTargetProvider<ELogDatadogTargetProvider>(this, "datadog")) {
        return false;
    }
#endif
    return true;
}

bool ELogMonSchemaHandler::registerMonTargetProvider(const char* monitorName,
                                                     ELogMonTargetProvider* provider) {
    return m_providerMap.insert(ProviderMap::value_type(monitorName, provider)).second;
}

ELogTarget* ELogMonSchemaHandler::loadTarget(const ELogConfigMapNode* logTargetCfg) {
    // the path represents the monitoring tool type
    // current predefined types are supported:
    // grafana_loki
    std::string monType;
    if (!ELogConfigLoader::getLogTargetStringProperty(logTargetCfg, "monitoring-tool", "type",
                                                      monType)) {
        return nullptr;
    }

    ProviderMap::iterator providerItr = m_providerMap.find(monType);
    if (providerItr != m_providerMap.end()) {
        ELogMonTargetProvider* provider = providerItr->second;
        return provider->loadTarget(logTargetCfg);
    }

    ELOG_REPORT_ERROR(
        "Invalid monitoring tool log target specification, unsupported type %s (context: %s)",
        monType.c_str(), logTargetCfg->getFullContext());
    return nullptr;
}

}  // namespace elog
