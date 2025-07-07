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

ELogTarget* ELogMsgQSchemaHandler::loadTarget(const ELogConfigMapNode* logTargetCfg) {
    // the path represents the message queue provider type name
    // current predefined types are supported:
    // kafka
    std::string msgQType;
    if (!ELogConfigLoader::getLogTargetStringProperty(logTargetCfg, "message queue", "type",
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
