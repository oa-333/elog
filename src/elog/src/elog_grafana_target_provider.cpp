#include "elog_grafana_target_provider.h"

#ifdef ELOG_ENABLE_GRAFANA_CONNECTOR

#include "elog_common.h"
#include "elog_config_loader.h"
#include "elog_error.h"
#include "elog_grafana_json_target.h"

namespace elog {

ELogMonTarget* ELogGrafanaTargetProvider::loadTarget(const std::string& logTargetCfg,
                                                     const ELogTargetSpec& targetSpec) {
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
    ELogPropertyMap::const_iterator itr = targetSpec.m_props.find("mode");
    if (itr == targetSpec.m_props.end()) {
        ELOG_REPORT_ERROR("Invalid Grafana log target specification, missing property mode: %s",
                          logTargetCfg.c_str());
        return nullptr;
    }
    const std::string& mode = itr->second;
    if (mode.compare("json") != 0) {
        ELOG_REPORT_ERROR(
            "Invalid Grafana log target specification, unsupported property mode=%s: %s",
            mode.c_str(), logTargetCfg.c_str());
        return nullptr;
    }

    itr = targetSpec.m_props.find("loki_endpoint");
    if (itr == targetSpec.m_props.end()) {
        ELOG_REPORT_ERROR(
            "Invalid Grafana log target specification, missing property loki_endpoint: %s",
            logTargetCfg.c_str());
        return nullptr;
    }
    const std::string& lokiEndpoint = itr->second;

    if (mode.compare("json") == 0) {
        itr = targetSpec.m_props.find("labels");
        if (itr == targetSpec.m_props.end()) {
            ELOG_REPORT_ERROR(
                "Invalid Grafana log target specification, missing property labels: %s",
                logTargetCfg.c_str());
            return nullptr;
        }
        const std::string& labels = itr->second;

        // optional log line metadata
        std::string logLineMetadata;
        itr = targetSpec.m_props.find("log_line_metadata");
        if (itr != targetSpec.m_props.end()) {
            logLineMetadata = itr->second;
        }

        // timeouts
        uint32_t connectTimeoutMillis = ELOG_GRAFANA_DEFAULT_CONNECT_TIMEOUT_MILLIS;
        itr = targetSpec.m_props.find("connect_timeout_millis");
        if (itr != targetSpec.m_props.end()) {
            if (!parseIntProp("connect_timeout_millis", logTargetCfg, itr->second,
                              connectTimeoutMillis)) {
                ELOG_REPORT_ERROR(
                    "Invalid Grafana log target specification, failed to parse connect timeout "
                    "'%s': %s",
                    itr->second.c_str(), logTargetCfg.c_str());
                return nullptr;
            }
        }

        uint32_t writeTimeoutMillis = ELOG_GRAFANA_DEFAULT_WRITE_TIMEOUT_MILLIS;
        itr = targetSpec.m_props.find("write_timeout_millis");
        if (itr != targetSpec.m_props.end()) {
            if (!parseIntProp("write_timeout_millis", logTargetCfg, itr->second,
                              writeTimeoutMillis)) {
                ELOG_REPORT_ERROR(
                    "Invalid Grafana log target specification, failed to parse write timeout "
                    "'%s': %s",
                    itr->second.c_str(), logTargetCfg.c_str());
                return nullptr;
            }
        }

        uint32_t readTimeoutMillis = ELOG_GRAFANA_DEFAULT_READ_TIMEOUT_MILLIS;
        itr = targetSpec.m_props.find("read_timeout_millis");
        if (itr != targetSpec.m_props.end()) {
            if (!parseIntProp("read_timeout_millis", logTargetCfg, itr->second,
                              readTimeoutMillis)) {
                ELOG_REPORT_ERROR(
                    "Invalid Grafana log target specification, failed to parse read timeout "
                    "'%s': %s",
                    itr->second.c_str(), logTargetCfg.c_str());
                return nullptr;
            }
        }

        ELogGrafanaJsonTarget* target = new (std::nothrow)
            ELogGrafanaJsonTarget(lokiEndpoint, connectTimeoutMillis, writeTimeoutMillis,
                                  readTimeoutMillis, labels.c_str(), logLineMetadata.c_str());
        if (target == nullptr) {
            ELOG_REPORT_ERROR("Failed to allocate Kafka message queue log target, out of memory");
        }
        return target;
    }
    return nullptr;
}

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
            ELogGrafanaJsonTarget(lokiEndpoint, connectTimeoutMillis, writeTimeoutMillis,
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
