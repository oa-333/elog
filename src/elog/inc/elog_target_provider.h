#ifndef __ELOG_TARGET_PROVIDER_H__
#define __ELOG_TARGET_PROVIDER_H__

#include "elog_config.h"
#include "elog_def.h"
#include "elog_target.h"

namespace elog {

/** @brief Factory class for creating log targets. */
class ELOG_API ELogTargetProvider {
public:
public:
    ELogTargetProvider(const ELogTargetProvider&) = delete;
    ELogTargetProvider(ELogTargetProvider&&) = delete;
    virtual ~ELogTargetProvider() {}
    ELogTargetProvider& operator=(const ELogTargetProvider&) = delete;

    /**
     * @brief Loads a target from configuration object.
     * @param logTargetCfg The configuration object.
     * @return ELogTarget* The resulting log target, or null if failed.
     */
    virtual ELogTarget* loadTarget(const ELogConfigMapNode* logTargetCfg) = 0;

protected:
    ELogTargetProvider() {}
};

}  // namespace elog

#endif  // __ELOG_TARGET_PROVIDER_H__