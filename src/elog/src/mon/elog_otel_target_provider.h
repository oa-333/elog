#ifndef __ELOG_OTEL_TARGET_PROVIDER_H__
#define __ELOG_OTEL_TARGET_PROVIDER_H__

#include "elog_def.h"

#ifdef ELOG_ENABLE_OTEL_CONNECTOR

#include "elog_target_provider.h"

namespace elog {

class ELogOtelTargetProvider : public ELogTargetProvider {
public:
    ELogOtelTargetProvider() {}
    ELogOtelTargetProvider(const ELogOtelTargetProvider&) = delete;
    ELogOtelTargetProvider(ELogOtelTargetProvider&&) = delete;
    ELogOtelTargetProvider& operator=(const ELogOtelTargetProvider&) = delete;
    ~ELogOtelTargetProvider() final {}

    /**
     * @brief Loads a target from configuration.
     * @param logTargetCfg The configuration string.
     * @return ELogTarget* The resulting log target, or null of failed.
     */
    ELogTarget* loadTarget(const ELogConfigMapNode* logTargetCfg) final;
};

}  // namespace elog

#endif  // ELOG_ENABLE_OTEL_CONNECTOR

#endif  // __ELOG_OTEL_TARGET_PROVIDER_H__