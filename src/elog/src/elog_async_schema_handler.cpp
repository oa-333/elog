#include "elog_async_schema_handler.h"

#include <cassert>

#include "elog_deferred_target_provider.h"
#include "elog_error.h"
#include "elog_quantum_target_provider.h"
#include "elog_queued_target_provider.h"

namespace elog {

template <typename T>
static bool initAsyncTargetProvider(ELogAsyncSchemaHandler* schemaHandler, const char* name) {
    T* provider = new (std::nothrow) T();
    if (provider == nullptr) {
        ELOG_REPORT_ERROR("Failed to create %s asynchronous target provider, out of memory", name);
        return false;
    }
    if (!schemaHandler->registerAsyncTargetProvider(name, provider)) {
        ELOG_REPORT_ERROR("Failed to register %s asynchronous target provider, duplicate name",
                          name);
        delete provider;
        return false;
    }
    return true;
}

ELogAsyncSchemaHandler::~ELogAsyncSchemaHandler() {
    for (auto& entry : m_providerMap) {
        delete entry.second;
    }
    m_providerMap.clear();
}

bool ELogAsyncSchemaHandler::registerPredefinedProviders() {
    // register predefined providers
    if (!initAsyncTargetProvider<ELogDeferredTargetProvider>(this, "deferred")) {
        return false;
    }
    if (!initAsyncTargetProvider<ELogQueuedTargetProvider>(this, "queued")) {
        return false;
    }
    if (!initAsyncTargetProvider<ELogQuantumTargetProvider>(this, "quantum")) {
        return false;
    }
    return true;
}

bool ELogAsyncSchemaHandler::registerAsyncTargetProvider(const char* asyncName,
                                                         ELogAsyncTargetProvider* provider) {
    return m_providerMap.insert(ProviderMap::value_type(asyncName, provider)).second;
}

ELogTarget* ELogAsyncSchemaHandler::loadTarget(const std::string& logTargetCfg,
                                               const ELogTargetSpec& targetSpec) {
    // asynchronous schema handler does NOT support URL style loading
    ELOG_REPORT_ERROR("Asynchronous log target does not support URL style loading: %s",
                      logTargetCfg.c_str());
    return nullptr;
}

ELogTarget* ELogAsyncSchemaHandler::loadTarget(const std::string& logTargetCfg,
                                               const ELogTargetNestedSpec& targetNestedSpec) {
    // the path represents the asynchronous target type name
    // current predefined types are supported:
    // deferred
    // queued
    // quantum
    ELogPropertyMap::const_iterator itr = targetNestedSpec.m_spec.m_props.find("type");
    if (itr == targetNestedSpec.m_spec.m_props.end()) {
        ELOG_REPORT_ERROR("Invalid asynchronous logging specification, missing type property: ",
                          logTargetCfg.c_str());
        return nullptr;
    }
    const std::string& asyncType = itr->second;

    // check we have a nested target first
    if (targetNestedSpec.m_subSpec.size() == 0) {
        ELOG_REPORT_ERROR("Invalid asynchronous logging specification, missing nested log target");
        return nullptr;
    }

    // get the provider and create the target
    ProviderMap::iterator providerItr = m_providerMap.find(asyncType);
    if (providerItr != m_providerMap.end()) {
        ELogAsyncTargetProvider* provider = providerItr->second;
        return provider->loadTarget(logTargetCfg, targetNestedSpec);
    }

    ELOG_REPORT_ERROR(
        "Invalid asynchronous log target specification, unsupported async type %s: %s",
        asyncType.c_str(), logTargetCfg.c_str());
    return nullptr;
}

}  // namespace elog
