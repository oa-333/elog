#ifndef __ELOG_DATADOG_TARGET_PROVIDER_H__
#define __ELOG_DATADOG_TARGET_PROVIDER_H__

#include "elog_def.h"

#ifdef ELOG_ENABLE_DATADOG_CONNECTOR

#include "elog_mon_target_provider.h"

namespace elog {

class ELogDatadogTargetProvider : public ELogMonTargetProvider {
public:
    ELogDatadogTargetProvider() {}
    ELogDatadogTargetProvider(const ELogDatadogTargetProvider&) = delete;
    ELogDatadogTargetProvider(ELogDatadogTargetProvider&&) = delete;
    ELogDatadogTargetProvider& operator=(const ELogDatadogTargetProvider&) = delete;
    ~ELogDatadogTargetProvider() final {}

    /**
     * @brief Loads a target from configuration.
     * @param logTargetCfg The configuration string.
     * @return ELogMonTarget* The resulting monitoring tool log target, or null of failed.
     */
    ELogMonTarget* loadTarget(const ELogConfigMapNode* logTargetCfg) final;
};

}  // namespace elog

#endif  // ELOG_ENABLE_DATADOG_CONNECTOR

#endif  // __ELOG_DATADOG_TARGET_PROVIDER_H__