#include "elog_kafka_msgq_target_provider.h"

#ifdef ELOG_ENABLE_KAFKA_MSGQ_CONNECTOR

#include "elog_common.h"
#include "elog_config_loader.h"
#include "elog_error.h"
#include "elog_kafka_msgq_target.h"

namespace elog {

ELogMsgQTarget* ELogKafkaMsgQTargetProvider::loadTarget(const ELogConfigMapNode* logTargetCfg,
                                                        const std::string& topic,
                                                        const std::string& headers) {
    // we expect 4 properties: kafka_bootstrap_servers, and optional partition,
    // kafka_flush_timeout_millis, kafka_shutdown_flush_timeout_millis
    std::string bootstrapServers;
    if (!ELogConfigLoader::getLogTargetStringProperty(
            logTargetCfg, "Kafka", "kafka_bootstrap_servers", bootstrapServers)) {
        return nullptr;
    }

    uint32_t flushTimeoutMillis = 0;
    if (!ELogConfigLoader::getOptionalLogTargetUInt32Property(
            logTargetCfg, "Kafka", "kafka_flush_timeout_millis", flushTimeoutMillis)) {
        return nullptr;
    }

    uint32_t shutdownFlushTimeoutMillis = 0;
    if (!ELogConfigLoader::getOptionalLogTargetUInt32Property(logTargetCfg, "Kafka",
                                                              "kafka_shutdown_flush_timeout_millis",
                                                              shutdownFlushTimeoutMillis)) {
        return nullptr;
    }

    int32_t partition = -1;
    if (!ELogConfigLoader::getOptionalLogTargetInt32Property(logTargetCfg, "Kafka", "partition",
                                                             partition)) {
        return nullptr;
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
