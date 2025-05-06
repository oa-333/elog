#include "elog_msgq_schema_handler.h"

#include <cassert>

#include "elog_kafka_msgq_target_provider.h"
#include "elog_system.h"

namespace elog {

template <typename T>
static bool initMsgQTargetProvider(ELogMsgQSchemaHandler* schemaHandler, const char* name) {
    T* provider = new (std::nothrow) T();
    if (provider == nullptr) {
        ELogSystem::reportError("Failed to create %s message queue target provider, out of memory",
                                name);
        return false;
    }
    if (!schemaHandler->registerMsgQTargetProvider(name, provider)) {
        ELogSystem::reportError(
            "Failed to register %s message queue target provider, duplicate name", name);
        delete provider;
        return false;
    }
    return true;
}

ELogMsgQSchemaHandler::ELogMsgQSchemaHandler() {
    // register predefined providers
#ifdef ELOG_ENABLE_KAFKA_MSGQ_CONNECTOR
    if (!initMsgQTargetProvider<ELogKafkaMsgQTargetProvider>(this, "kafka")) {
        assert(false);
    }
#endif
}

ELogMsgQSchemaHandler::~ELogMsgQSchemaHandler() {
    // cleanup provider map
}

bool ELogMsgQSchemaHandler::registerMsgQTargetProvider(const char* brokerName,
                                                       ELogMsgQTargetProvider* provider) {
    return m_providerMap.insert(ProviderMap::value_type(brokerName, provider)).second;
}

ELogTarget* ELogMsgQSchemaHandler::loadTarget(const std::string& logTargetCfg,
                                              const ELogTargetSpec& targetSpec) {
    // the path represents the msgq-type
    // current predefined types are supported:
    // kafka
    const std::string& msgQType = targetSpec.m_path;

    // in addition, we expect at least one properties: topic
    if (targetSpec.m_props.size() < 1) {
        ELogSystem::reportError(
            "Invalid message queue log target specification, expected at least one property: %s",
            logTargetCfg.c_str());
        return nullptr;
    }

    ELogPropertyMap::const_iterator itr = targetSpec.m_props.find("topic");
    if (itr == targetSpec.m_props.end()) {
        ELogSystem::reportError(
            "Invalid message queue log target specification, missing property topic: %s",
            logTargetCfg.c_str());
        return nullptr;
    }
    const std::string& topic = itr->second;

    ProviderMap::iterator providerItr = m_providerMap.find(msgQType);
    if (providerItr != m_providerMap.end()) {
        ELogMsgQTargetProvider* provider = providerItr->second;
        return provider->loadTarget(logTargetCfg, targetSpec, topic);
    }

    ELogSystem::reportError(
        "Invalid message queue log target specification, unsupported message queue type %s: %s",
        msgQType.c_str(), logTargetCfg.c_str());
    return nullptr;
}

}  // namespace elog
