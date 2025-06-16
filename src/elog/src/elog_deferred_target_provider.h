#ifndef __ELOG_DEFERRED_TARGET_PROVIDER_H__
#define __ELOG_DEFERRED_TARGET_PROVIDER_H__

#include "elog_async_target_provider.h"

namespace elog {

class ELogDeferredTargetProvider : public ELogAsyncTargetProvider {
public:
    ELogDeferredTargetProvider() {}
    ELogDeferredTargetProvider(const ELogDeferredTargetProvider&) = delete;
    ELogDeferredTargetProvider(ELogDeferredTargetProvider&&) = delete;
    ~ELogDeferredTargetProvider() final {}

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

    /**
     * @brief Loads a target from configuration object.
     * @param logTargetCfg The configuration object.
     * @return ELogAsyncTarget* The resulting log target, or null if failed.
     */
    ELogAsyncTarget* loadTarget(const ELogConfigMapNode* logTargetCfg) final;
};

}  // namespace elog

#endif  // __ELOG_DEFERRED_TARGET_PROVIDER_H__