#ifndef __ELOG_QUEUED_TARGET_PROVIDER_H__
#define __ELOG_QUEUED_TARGET_PROVIDER_H__

#include "async/elog_async_target_provider.h"

namespace elog {

class ELogQueuedTargetProvider : public ELogAsyncTargetProvider {
public:
    ELogQueuedTargetProvider() {}
    ELogQueuedTargetProvider(const ELogQueuedTargetProvider&) = delete;
    ELogQueuedTargetProvider(ELogQueuedTargetProvider&&) = delete;
    ELogQueuedTargetProvider& operator=(const ELogQueuedTargetProvider&) = delete;
    ~ELogQueuedTargetProvider() final {}

    /**
     * @brief Loads a target from configuration object.
     * @param logTargetCfg The configuration object.
     * @return ELogAsyncTarget* The resulting log target, or null if failed.
     */
    ELogTarget* loadTarget(const ELogConfigMapNode* logTargetCfg) final;
};

}  // namespace elog

#endif  // __ELOG_QUEUED_TARGET_PROVIDER_H__