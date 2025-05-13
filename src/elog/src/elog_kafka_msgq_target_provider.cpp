#include "elog_kafka_msgq_target_provider.h"

#ifdef ELOG_ENABLE_KAFKA_MSGQ_CONNECTOR

#include "elog_common.h"
#include "elog_error.h"
#include "elog_kafka_msgq_target.h"

namespace elog {

ELogMsgQTarget* ELogKafkaMsgQTargetProvider::loadTarget(const std::string& logTargetCfg,
                                                        const ELogTargetSpec& targetSpec,
                                                        const std::string& topic,
                                                        const std::string& headers) {
    // we expect 4 properties: bootstrap-servers, and optional partition,  flush-timeout-millis,
    // shutdown-flush-timeout-millis
    ELogPropertyMap::const_iterator itr = targetSpec.m_props.find("bootstrap-servers");
    if (itr == targetSpec.m_props.end()) {
        ELOG_REPORT_ERROR(
            "Invalid Kafka message queue log target specification, missing property "
            "bootstrap-servers: %s",
            logTargetCfg.c_str());
        return nullptr;
    }
    const std::string& bootstrapServers = itr->second;

    uint32_t flushTimeoutMillis = -1;
    itr = targetSpec.m_props.find("kafka-flush-timeout-millis");
    if (itr != targetSpec.m_props.end()) {
        if (!parseIntProp("kafka-flush-timeout-millis", logTargetCfg, itr->second,
                          flushTimeoutMillis, true)) {
            ELOG_REPORT_ERROR(
                "Invalid  log target specification, invalid flush timeout millis id: %s",
                logTargetCfg.c_str());
            return nullptr;
        }
    }

    uint32_t shutdownFlushTimeoutMillis = -1;
    itr = targetSpec.m_props.find("kafka-shutdown-flush-timeout-millis");
    if (itr != targetSpec.m_props.end()) {
        if (!parseIntProp("kafka-shutdown-flush-timeout-millis", logTargetCfg, itr->second,
                          shutdownFlushTimeoutMillis, true)) {
            ELOG_REPORT_ERROR(
                "Invalid  log target specification, invalid shutdown flush timeout millis id: %s",
                logTargetCfg.c_str());
            return nullptr;
        }
    }

    int partition = -1;
    itr = targetSpec.m_props.find("partition");
    if (itr != targetSpec.m_props.end()) {
        uint32_t part = 0;
        if (!parseIntProp("partition", logTargetCfg, itr->second, part, true)) {
            ELOG_REPORT_ERROR("Invalid  log target specification, invalid partition id: %s",
                              logTargetCfg.c_str());
            return nullptr;
        }
        partition = part;
    }

    ELogMsgQTarget* target =
        new (std::nothrow) ELogKafkaMsgQTarget(bootstrapServers, topic, headers, partition,
                                               flushTimeoutMillis, shutdownFlushTimeoutMillis);
    if (target == nullptr) {
        ELOG_REPORT_ERROR("Failed to allocate Kafka message queue log target, out of memory");
    }
    return target;
}

}  // namespace elog

#endif  // ELOG_ENABLE_KAFKA_MSGQ_CONNECTOR
