#include "elog_grafana_target_provider.h"

#ifdef ELOG_ENABLE_GRAFANA_CONNECTOR

#include "elog_common.h"
#include "elog_config_loader.h"
#include "elog_error.h"
#include "elog_grafana_json_target.h"
#include "elog_http_config_loader.h"

namespace elog {

ELogMonTarget* ELogGrafanaTargetProvider::loadTarget(const ELogConfigMapNode* logTargetCfg) {
    // expected url is as follows:
    // mon://grafana?mode=json&
    //  loki_address=http://host:port&
    //  labels={JSON_FORMAT}&
    //  log_line_metadata={JSON_FORMAT}&
    //  connect_timeout_millis=value&
    //  write_timeout_millis=value&
    //  read_timeout_millis=value&
    //  resend_period_millis=value&
    //  backlog_limit_bytes=value&
    //  shutdown_timeout_millis=value

    // NOTE: The JSON_FORMAT above is permissive and should not contains any quotes

    // we expect at most 9 properties, of which only mode, loki_address and labels are mandatory:
    // mode=json/grpc, loki_address, msg labels, line metadata, connect/read/write/shutdown
    // timeouts, backlog size
    // aggregation may be controlled by flush policy
    // labels are usually some outer metadata, some static, some extracted from env
    // line metadata is normal field selector stuff
    std::string mode;
    if (!ELogConfigLoader::getLogTargetStringProperty(logTargetCfg, "Grafana-Loki", "mode", mode)) {
        return nullptr;
    }
    if (mode.compare("json") != 0) {
        ELOG_REPORT_ERROR(
            "Invalid Grafana log target specification, unsupported property 'mode' value '%s' "
            "(context: %s)",
            mode.c_str(), logTargetCfg->getFullContext());
        return nullptr;
    }

    // load common HTTP configuration (use defaults, see ELogHttpConfig default constructor)
    ELogHttpConfig httpConfig;
    if (!ELogHttpConfigLoader::loadHttpConfig(logTargetCfg, "Grafana-Loki", httpConfig)) {
        ELOG_REPORT_ERROR(
            "Invalid Grafana log target specification, invalid HTTP properties (context: %s)",
            logTargetCfg->getFullContext());
        return nullptr;
    }

    std::string lokiAddress;
    if (!ELogConfigLoader::getLogTargetStringProperty(logTargetCfg, "Grafana-Loki", "loki_address",
                                                      lokiAddress)) {
        return nullptr;
    }

    std::string labels;
    if (!ELogConfigLoader::getLogTargetStringProperty(logTargetCfg, "Grafana-Loki", "labels",
                                                      labels)) {
        return nullptr;
    }

    // optional log line metadata
    std::string logLineMetadata;
    if (!ELogConfigLoader::getOptionalLogTargetStringProperty(
            logTargetCfg, "Grafana-Loki", "log_line_metadata", logLineMetadata)) {
        return nullptr;
    }

    if (mode.compare("json") == 0) {
        ELogGrafanaJsonTarget* target = new (std::nothrow) ELogGrafanaJsonTarget(
            lokiAddress.c_str(), httpConfig, labels.c_str(), logLineMetadata.c_str());
        if (target == nullptr) {
            ELOG_REPORT_ERROR("Failed to allocate Grafana-Loki log target, out of memory");
        }
        return target;
    }
    return nullptr;
}

}  // namespace elog

#endif  // ELOG_ENABLE_GRAFANA_CONNECTOR
