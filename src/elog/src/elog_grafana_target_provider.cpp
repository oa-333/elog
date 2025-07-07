#include "elog_grafana_target_provider.h"

#ifdef ELOG_ENABLE_GRAFANA_CONNECTOR

#include "elog_common.h"
#include "elog_config_loader.h"
#include "elog_error.h"
#include "elog_grafana_json_target.h"

namespace elog {

ELogMonTarget* ELogGrafanaTargetProvider::loadTarget(const ELogConfigMapNode* logTargetCfg) {
    // expected url is as follows:
    // mon://grafana?mode=json&
    //  loki_endpoint=http://host:port&
    //  labels={JSON_FORMAT}&
    //  log_line_metadata={JSON_FORMAT}&
    //  connect_timeout_millis=value&
    //  write_timeout_millis=value&
    //  read_timeout_millis=value&

    // we expect 4 properties: mode=json/grpc, loki_endpoint, msg labels, line metadata, connect
    // timeout, write timeout, aggregation may be controlled by flush policy
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

    std::string lokiEndpoint;
    if (!ELogConfigLoader::getLogTargetStringProperty(logTargetCfg, "Grafana-Loki", "loki_endpoint",
                                                      lokiEndpoint)) {
        return nullptr;
    }

    if (mode.compare("json") == 0) {
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

        // timeouts
        int64_t connectTimeoutMillis = ELOG_GRAFANA_DEFAULT_CONNECT_TIMEOUT_MILLIS;
        if (!ELogConfigLoader::getOptionalLogTargetIntProperty(
                logTargetCfg, "Grafana-Loki", "connect_timeout_millis", connectTimeoutMillis)) {
            return nullptr;
        }

        int64_t writeTimeoutMillis = ELOG_GRAFANA_DEFAULT_WRITE_TIMEOUT_MILLIS;
        if (!ELogConfigLoader::getOptionalLogTargetIntProperty(
                logTargetCfg, "Grafana-Loki", "write_timeout_millis", writeTimeoutMillis)) {
            return nullptr;
        }

        int64_t readTimeoutMillis = ELOG_GRAFANA_DEFAULT_READ_TIMEOUT_MILLIS;
        if (!ELogConfigLoader::getOptionalLogTargetIntProperty(
                logTargetCfg, "Grafana-Loki", "read_timeout_millis", readTimeoutMillis)) {
            return nullptr;
        }

        ELogGrafanaJsonTarget* target = new (std::nothrow)
            ELogGrafanaJsonTarget(lokiEndpoint.c_str(), connectTimeoutMillis, writeTimeoutMillis,
                                  readTimeoutMillis, labels.c_str(), logLineMetadata.c_str());
        if (target == nullptr) {
            ELOG_REPORT_ERROR("Failed to allocate Grafana-Loki log target, out of memory");
        }
        return target;
    }
    return nullptr;
}

}  // namespace elog

#endif  // ELOG_ENABLE_GRAFANA_CONNECTOR
