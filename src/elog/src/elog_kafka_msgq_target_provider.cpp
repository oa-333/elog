#include "elog_kafka_msgq_target_provider.h"

#ifdef ELOG_ENABLE_KAFKA_MSGQ_CONNECTOR

#include "elog_common.h"
#include "elog_kafka_msgq_target.h"
#include "elog_system.h"

namespace elog {

ELogMsgQTarget* ELogKafkaMsgQTargetProvider::loadTarget(const std::string& logTargetCfg,
                                                        const ELogTargetSpec& targetSpec,
                                                        const std::string& topic) {
    // we expect 4 properties: bootstrap-servers, and optional partition
    ELogPropertyMap::const_iterator itr = targetSpec.m_props.find("bootstrap-servers");
    if (itr == targetSpec.m_props.end()) {
        ELogSystem::reportError(
            "Invalid Kafka message queue log target specification, missing property "
            "bootstrap-servers: %s",
            logTargetCfg.c_str());
        return nullptr;
    }
    const std::string& bootstrapServers = itr->second;

    int partition = -1;
    itr = targetSpec.m_props.find("partition");
    if (itr != targetSpec.m_props.end()) {
        uint32_t part = 0;
        if (!parseIntProp("partition", logTargetCfg, itr->second, part, true)) {
            ELogSystem::reportError("Invalid  log target specification, invalid partition id: %s",
                                    logTargetCfg.c_str());
            return nullptr;
        }
        partition = part;
    }

    ELogMsgQTarget* target =
        new (std::nothrow) ELogKafkaMsgQTarget(bootstrapServers, topic, partition);
    if (target == nullptr) {
        ELogSystem::reportError("Failed to allocate Kafka message queue log target, out of memory");
    }
    return target;
}

}  // namespace elog

#endif  // ELOG_ENABLE_KAFKA_MSGQ_CONNECTOR
