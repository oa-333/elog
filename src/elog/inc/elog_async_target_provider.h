#ifndef __ELOG_ASYNC_TARGET_PROVIDER_H__
#define __ELOG_ASYNC_TARGET_PROVIDER_H__

#include "elog_async_target.h"
#include "elog_target_spec.h"

namespace elog {

/** @brief Parent interface for all asynchronous log target providers. */
class ELOG_API ELogAsyncTargetProvider {
public:
    ELogAsyncTargetProvider(const ELogAsyncTargetProvider&) = delete;
    ELogAsyncTargetProvider(ELogAsyncTargetProvider&&) = delete;
    virtual ~ELogAsyncTargetProvider() {}

    /**
     * @brief Loads a target from configuration object.
     * @param logTargetCfg The configuration object.
     * @return ELogAsyncTarget* The resulting log target, or null if failed.
     */
    virtual ELogAsyncTarget* loadTarget(const ELogConfigMapNode* logTargetCfg) = 0;

protected:
    ELogAsyncTargetProvider() {}

    ELogTarget* loadNestedTarget(const ELogConfigMapNode* logTargetCfg);
};

}  // namespace elog

#endif  // __ELOG_ASYNC_TARGET_PROVIDER_H__