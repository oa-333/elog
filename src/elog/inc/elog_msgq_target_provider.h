#ifndef __ELOG_MSGQ_TARGET_PROVIDER_H__
#define __ELOG_MSGQ_TARGET_PROVIDER_H__

#include "elog_msgq_target.h"
#include "elog_target_spec.h"

namespace elog {

/** @brief Parent interface for all DB log targets. */
class ELOG_API ELogMsgQTargetProvider {
public:
    ELogMsgQTargetProvider(const ELogMsgQTargetProvider&) = delete;
    ELogMsgQTargetProvider(ELogMsgQTargetProvider&&) = delete;
    virtual ~ELogMsgQTargetProvider() {}

    /**
     * @brief Loads a target from configuration.
     * @param logTargetCfg The configuration string.
     * @param targetSpec The parsed configuration string.
     * @param topic The target topic name.
     * @param headers Optional headers specification (in property-CSV format: "header-name=${field},
     * header-name=${field}, ...").
     * @return ELogMsgQTarget* The resulting message queue log target, or null of failed.
     */
    virtual ELogMsgQTarget* loadTarget(const std::string& logTargetCfg,
                                       const ELogTargetSpec& targetSpec, const std::string& topic,
                                       const std::string& headers) = 0;

    /**
     * @brief Loads a target from configuration.
     * @param logTargetCfg The configuration object.
     * @param topic The target topic name.
     * @param headers Optional headers specification (in property-CSV format: "header-name=${field},
     * header-name=${field}, ...").
     * @return ELogMsgQTarget* The resulting message queue log target, or null of failed.
     */
    virtual ELogMsgQTarget* loadTarget(const ELogConfigMapNode* logTargetCfg,
                                       const std::string& topic, const std::string& headers) = 0;

protected:
    ELogMsgQTargetProvider() {}
};

}  // namespace elog

#endif  // __ELOG_MSGQ_TARGET_PROVIDER_H__