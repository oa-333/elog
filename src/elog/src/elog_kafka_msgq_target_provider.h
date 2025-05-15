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
    ~ELogKafkaMsgQTargetProvider() final {}

    ELogMsgQTarget* loadTarget(const std::string& logTargetCfg, const ELogTargetSpec& targetSpec,
                               const std::string& topic, const std::string& headers) final;
};

}  // namespace elog

#endif  // ELOG_ENABLE_KAFKA_MSGQ_CONNECTOR

#endif  // __ELOG_KAFKA_MSGQ_TARGET_PROVIDER_H__