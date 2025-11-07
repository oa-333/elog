#ifndef __ELOG_ASYNC_TARGET_PROVIDER_H__
#define __ELOG_ASYNC_TARGET_PROVIDER_H__

#include "elog_async_target.h"
#include "elog_target_provider.h"
#include "elog_target_spec.h"

namespace elog {

/** @brief Parent interface for all asynchronous log target providers. */
class ELOG_API ELogAsyncTargetProvider : public ELogTargetProvider {
public:
    ELogAsyncTargetProvider(const ELogAsyncTargetProvider&) = delete;
    ELogAsyncTargetProvider(ELogAsyncTargetProvider&&) = delete;
    virtual ~ELogAsyncTargetProvider() {}
    ELogAsyncTargetProvider& operator=(const ELogAsyncTargetProvider&) = delete;

protected:
    ELogAsyncTargetProvider() {}

    ELogTarget* loadNestedTarget(const ELogConfigMapNode* logTargetCfg);
};

}  // namespace elog

#endif  // __ELOG_ASYNC_TARGET_PROVIDER_H__