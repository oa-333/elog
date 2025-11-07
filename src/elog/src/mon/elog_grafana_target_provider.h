#ifndef __ELOG_GRAFANA_TARGET_PROVIDER_H__
#define __ELOG_GRAFANA_TARGET_PROVIDER_H__

#include "elog_def.h"

#ifdef ELOG_ENABLE_GRAFANA_CONNECTOR

#include "elog_target_provider.h"

namespace elog {

class ELogGrafanaTargetProvider : public ELogTargetProvider {
public:
    ELogGrafanaTargetProvider() {}
    ELogGrafanaTargetProvider(const ELogGrafanaTargetProvider&) = delete;
    ELogGrafanaTargetProvider(ELogGrafanaTargetProvider&&) = delete;
    ELogGrafanaTargetProvider& operator=(const ELogGrafanaTargetProvider&) = delete;
    ~ELogGrafanaTargetProvider() final {}

    /**
     * @brief Loads a target from configuration.
     * @param logTargetCfg The configuration string.ing.
     * @return ELogTarget* The resulting log target, or null of failed.
     */
    ELogTarget* loadTarget(const ELogConfigMapNode* logTargetCfg) final;
};

}  // namespace elog

#endif  // ELOG_ENABLE_GRAFANA_CONNECTOR

#endif  // __ELOG_GRAFANA_TARGET_PROVIDER_H__