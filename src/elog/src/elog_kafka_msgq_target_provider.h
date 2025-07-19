#ifndef __ELOG_KAFKA_MSGQ_TARGET_PROVIDER_H__
#define __ELOG_KAFKA_MSGQ_TARGET_PROVIDER_H__

#include "elog_def.h"

#ifdef ELOG_ENABLE_KAFKA_MSGQ_CONNECTOR

#include "elog_msgq_target_provider.h"

namespace elog {

class ELogKafkaMsgQTargetProvider : public ELogMsgQTargetProvider {
public:
    ELogKafkaMsgQTargetProvider() {}
    ELogKafkaMsgQTargetProvider(const ELogKafkaMsgQTargetProvider&) = delete;
    ELogKafkaMsgQTargetProvider(ELogKafkaMsgQTargetProvider&&) = delete;
    ELogKafkaMsgQTargetProvider& operator=(const ELogKafkaMsgQTargetProvider&) = delete;
    ~ELogKafkaMsgQTargetProvider() final {}

    /**
     * @brief Loads a target from configuration.
     * @param logTargetCfg The configuration object.
     * @param topic The target topic name.
     * @param headers Optional headers specification (in property-CSV format: "header-name=${field},
     * header-name=${field}, ...").
     * @return ELogMsgQTarget* The resulting message queue log target, or null of failed.
     */
    ELogMsgQTarget* loadTarget(const ELogConfigMapNode* logTargetCfg, const std::string& topic,
                               const std::string& headers) final;
};

}  // namespace elog

#endif  // ELOG_ENABLE_KAFKA_MSGQ_CONNECTOR

#endif  // __ELOG_KAFKA_MSGQ_TARGET_PROVIDER_H__