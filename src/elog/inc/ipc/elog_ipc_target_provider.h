#ifndef __ELOG_IPC_TARGET_PROVIDER_H__
#define __ELOG_IPC_TARGET_PROVIDER_H__

#ifdef ELOG_ENABLE_IPC

#include "elog_config.h"
#include "elog_target.h"
#include "elog_target_spec.h"

namespace elog {

/** @brief Parent interface for all IPC log targets. */
class ELOG_API ELogIpcTargetProvider {
public:
    ELogIpcTargetProvider() {}
    ELogIpcTargetProvider(const ELogIpcTargetProvider&) = delete;
    ELogIpcTargetProvider(ELogIpcTargetProvider&&) = delete;
    ELogIpcTargetProvider& operator=(const ELogIpcTargetProvider&) = delete;
    virtual ~ELogIpcTargetProvider() {}

    /**
     * @brief Loads a target from configuration.
     * @param logTargetCfg The configuration string.
     * @return ELogIpcTarget* The resulting IPC log target, or null of failed.
     */
    virtual ELogTarget* loadTarget(const ELogConfigMapNode* logTargetCfg) = 0;
};

}  // namespace elog

#endif  // ELOG_ENABLE_IPC

#endif  // __ELOG_IPC_TARGET_PROVIDER_H__