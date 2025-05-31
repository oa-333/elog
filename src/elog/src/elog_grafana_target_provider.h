#ifndef __ELOG_GRAFANA_TARGET_PROVIDER_H__
#define __ELOG_GRAFANA_TARGET_PROVIDER_H__

#include "elog_def.h"

#ifdef ELOG_ENABLE_GRAFANA_CONNECTOR

#include "elog_mon_target_provider.h"

namespace elog {

class ELogGrafanaTargetProvider : public ELogMonTargetProvider {
public:
    ELogGrafanaTargetProvider() {}
    ELogGrafanaTargetProvider(const ELogGrafanaTargetProvider&) = delete;
    ELogGrafanaTargetProvider(ELogGrafanaTargetProvider&&) = delete;
    ~ELogGrafanaTargetProvider() final {}

    /**
     * @brief Loads a target from configuration.
     * @param logTargetCfg The configuration string.
     * @param targetSpec The parsed configuration string.
     * @return ELogMonTarget* The resulting monitoring tool log target, or null of failed.
     */
    ELogMonTarget* loadTarget(const std::string& logTargetCfg,
                              const ELogTargetSpec& targetSpec) final;
};

}  // namespace elog

#endif  // ELOG_ENABLE_GRAFANA_CONNECTOR

#endif  // __ELOG_GRAFANA_TARGET_PROVIDER_H__