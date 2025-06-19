#ifndef __ELOG_SENTRY_TARGET_PROVIDER_H__
#define __ELOG_SENTRY_TARGET_PROVIDER_H__

#include "elog_def.h"

#ifdef ELOG_ENABLE_SENTRY_CONNECTOR

#include "elog_mon_target_provider.h"

namespace elog {

class ELogSentryTargetProvider : public ELogMonTargetProvider {
public:
    ELogSentryTargetProvider() {}
    ELogSentryTargetProvider(const ELogSentryTargetProvider&) = delete;
    ELogSentryTargetProvider(ELogSentryTargetProvider&&) = delete;
    ~ELogSentryTargetProvider() final {}

    /**
     * @brief Loads a target from configuration.
     * @param logTargetCfg The configuration string.
     * @param targetSpec The parsed configuration string.
     * @return ELogMonTarget* The resulting monitoring tool log target, or null of failed.
     */
    ELogMonTarget* loadTarget(const std::string& logTargetCfg,
                              const ELogTargetSpec& targetSpec) final;

    /**
     * @brief Loads a target from configuration.
     * @param logTargetCfg The configuration string.
     * @param targetSpec The parsed configuration string.
     * @return ELogMonTarget* The resulting monitoring tool log target, or null of failed.
     */
    ELogMonTarget* loadTarget(const ELogConfigMapNode* logTargetCfg) final;
};

}  // namespace elog

#endif  // ELOG_ENABLE_SENTRY_CONNECTOR

#endif  // __ELOG_SENTRY_TARGET_PROVIDER_H__