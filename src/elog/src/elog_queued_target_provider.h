#ifndef __ELOG_QUEUED_TARGET_PROVIDER_H__
#define __ELOG_QUEUED_TARGET_PROVIDER_H__

#include "elog_async_target_provider.h"

namespace elog {

class ELogQueuedTargetProvider : public ELogAsyncTargetProvider {
public:
    ELogQueuedTargetProvider() {}
    ELogQueuedTargetProvider(const ELogQueuedTargetProvider&) = delete;
    ELogQueuedTargetProvider(ELogQueuedTargetProvider&&) = delete;
    ~ELogQueuedTargetProvider() final {}

    /**
     * @brief Loads a target from configuration (URL style).
     * @param logTargetCfg The configuration string.
     * @param targetSpec The parsed configuration string.
     * @return ELogAsyncTarget* The resulting DB log target, or null of failed.
     */
    ELogAsyncTarget* loadTarget(const std::string& logTargetCfg,
                                const ELogTargetSpec& targetSpec) final;

    /**
     * @brief Loads a target from configuration (nested style).
     * @param logTargetCfg The configuration string.
     * @param targetNestedSpec The parsed configuration string.
     * @return ELogAsyncTarget* The resulting DB log target, or null of failed.
     */
    ELogAsyncTarget* loadTarget(const std::string& logTargetCfg,
                                const ELogTargetNestedSpec& targetSpec) final;
};

}  // namespace elog

#endif  // __ELOG_QUEUED_TARGET_PROVIDER_H__