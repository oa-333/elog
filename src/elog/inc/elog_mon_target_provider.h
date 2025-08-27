#ifndef __ELOG_MON_TARGET_PROVIDER_H__
#define __ELOG_MON_TARGET_PROVIDER_H__

#include "elog_config.h"
#include "elog_mon_target.h"
#include "elog_target_spec.h"

namespace elog {

/** @brief Parent interface for all monitoring tool log targets. */
class ELOG_API ELogMonTargetProvider {
public:
    ELogMonTargetProvider(const ELogMonTargetProvider&) = delete;
    ELogMonTargetProvider(ELogMonTargetProvider&&) = delete;
    ELogMonTargetProvider& operator=(const ELogMonTargetProvider&) = delete;
    virtual ~ELogMonTargetProvider() {}

    /**
     * @brief Loads a target from configuration.
     * @param logTargetCfg The configuration string.
     * @return ELogMonTarget* The resulting monitoring tool log target, or null of failed.
     */
    virtual ELogMonTarget* loadTarget(const ELogConfigMapNode* logTargetCfg) = 0;

protected:
    ELogMonTargetProvider() {}
};

}  // namespace elog

#endif  // __ELOG_MON_TARGET_PROVIDER_H__