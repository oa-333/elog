#include "elog_msgq_schema_handler.h"

#include <cassert>

#include "elog_config_loader.h"
#include "elog_error.h"
#include "elog_kafka_msgq_target_provider.h"

namespace elog {

template <typename T>
static bool initMsgQTargetProvider(ELogMsgQSchemaHandler* schemaHandler, const char* name) {
    T* provider = new (std::nothrow) T();
    if (provider == nullptr) {
        ELOG_REPORT_ERROR("Failed to create %s message queue target provider, out of memory", name);
        return false;
    }
    if (!schemaHandler->registerMsgQTargetProvider(name, provider)) {
        ELOG_REPORT_ERROR("Failed to register %s message queue target provider, duplicate name",
                          name);
        delete provider;
        return false;
    }
    return true;
}

ELogMsgQSchemaHandler::~ELogMsgQSchemaHandler() {
    for (auto& entry : m_providerMap) {
        delete entry.second;
    }
    m_providerMap.clear();
}

bool ELogMsgQSchemaHandler::registerPredefinedProviders() {
    // register predefined providers
#ifdef ELOG_ENABLE_KAFKA_MSGQ_CONNECTOR
    if (!initMsgQTargetProvider<ELogKafkaMsgQTargetProvider>(this, "kafka")) {
        return false;
    }
#endif
    return true;
}

bool ELogMsgQSchemaHandler::registerMsgQTargetProvider(const char* brokerName,
                                                       ELogMsgQTargetProvider* provider) {
    return m_providerMap.insert(ProviderMap::value_type(brokerName, provider)).second;
}

ELogTarget* ELogMsgQSchemaHandler::loadTarget(const std::string& logTargetCfg,
                                              const ELogTargetSpec& targetSpec) {
    // the path represents the message queue provider type name
    // current predefined types are supported:
    // kafka
    const std::string& msgQType = targetSpec.m_path;

    // in addition, we expect at least 'msgq_topic' property and optional 'msgq_headers'
    if (targetSpec.m_props.size() < 1) {
        ELOG_REPORT_ERROR(
            "Invalid message queue log target specification, expected at least one property: %s",
            logTargetCfg.c_str());
        return nullptr;
    }

    ELogPropertyMap::const_iterator itr = targetSpec.m_props.find("msgq_topic");
    if (itr == targetSpec.m_props.end()) {
        ELOG_REPORT_ERROR(
            "Invalid message queue log target specification, missing property msgq_topic: %s",
            logTargetCfg.c_str());
        return nullptr;
    }
    const std::string& topic = itr->second;

    std::string headers;
    itr = targetSpec.m_props.find("msgq_headers");
    if (itr != targetSpec.m_props.end()) {
        headers = itr->second;
    }

    ProviderMap::iterator providerItr = m_providerMap.find(msgQType);
    if (providerItr != m_providerMap.end()) {
        ELogMsgQTargetProvider* provider = providerItr->second;
        return provider->loadTarget(logTargetCfg, targetSpec, topic, headers);
    }

    ELOG_REPORT_ERROR(
        "Invalid message queue log target specification, unsupported message queue type %s: %s",
        msgQType.c_str(), logTargetCfg.c_str());
    return nullptr;
}

ELogTarget* ELogMsgQSchemaHandler::loadTarget(const std::string& logTargetCfg,
                                              const ELogTargetNestedSpec& targetNestedSpec) {
    // first make sure there ar no log target sub-specs
    if (targetNestedSpec.m_subSpec.find("log_target") != targetNestedSpec.m_subSpec.end()) {
        ELOG_REPORT_ERROR("Message queue log target cannot have sub-targets: %s",
                          logTargetCfg.c_str());
        return nullptr;
    }

    // implementation is identical to URL style
    return loadTarget(logTargetCfg, targetNestedSpec.m_spec);
}

ELogTarget* ELogMsgQSchemaHandler::loadTarget(const ELogConfigMapNode* logTargetCfg) {
    // the path represents the message queue provider type name
    // current predefined types are supported:
    // kafka
    std::string msgQType;
    if (!ELogConfigLoader::getLogTargetStringProperty(logTargetCfg, "message queue", "path",
                                                      msgQType)) {
        return nullptr;
    }

    std::string topic;
    if (!ELogConfigLoader::getLogTargetStringProperty(logTargetCfg, "message queue", "msgq_topic",
                                                      topic)) {
        return nullptr;
    }

    std::string headers;
    if (!ELogConfigLoader::getOptionalLogTargetStringProperty(logTargetCfg, "message queue",
                                                              "msgq_headers", headers)) {
        return nullptr;
    }

    ProviderMap::iterator providerItr = m_providerMap.find(msgQType);
    if (providerItr != m_providerMap.end()) {
        ELogMsgQTargetProvider* provider = providerItr->second;
        return provider->loadTarget(logTargetCfg, topic, headers);
    }

    ELOG_REPORT_ERROR(
        "Invalid message queue log target specification, unsupported message queue type %s "
        "(context: %s)",
        msgQType.c_str(), logTargetCfg->getFullContext());
    return nullptr;
}

}  // namespace elog
