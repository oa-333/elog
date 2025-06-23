#include "elog_datadog_target_provider.h"

#ifdef ELOG_ENABLE_DATADOG_CONNECTOR

#include "elog_common.h"
#include "elog_config_loader.h"
#include "elog_datadog_target.h"
#include "elog_error.h"

namespace elog {

ELogMonTarget* ELogDatadogTargetProvider::loadTarget(const std::string& logTargetCfg,
                                                     const ELogTargetSpec& targetSpec) {
    // expected url is as follows:
    // mon://datadog?
    //  endpoint=http://host:port&  // e.g. endpoint=https://http-intake.logs.datadoghq.com
    //  api_key=<key>&
    //  source=<name>&
    //  service=<name>&
    //  tags={JSON_FORMAT}&
    //  stack_trace=yes/no&
    //  compress=yes/no&
    //  connect_timeout_millis=value&
    //  write_timeout_millis=value&
    //  read_timeout_millis=value

    // we expect the following properties: endpoint and key (mandatory), tags, source, service,
    // compress and connect timeout, write timeout, aggregation may be controlled by flush policy
    // tags is normal field selector stuff
    ELogPropertyMap::const_iterator itr = targetSpec.m_props.find("endpoint");
    if (itr == targetSpec.m_props.end()) {
        ELOG_REPORT_ERROR(
            "Invalid Datadog log target specification, missing property 'endpoint': %s",
            logTargetCfg.c_str());
        return nullptr;
    }
    const std::string& endpoint = itr->second;

    itr = targetSpec.m_props.find("api_key");
    if (itr == targetSpec.m_props.end()) {
        ELOG_REPORT_ERROR(
            "Invalid Datadog log target specification, missing property 'api_key': %s",
            logTargetCfg.c_str());
        return nullptr;
    }
    const std::string& apiKey = itr->second;

    // optional source
    std::string source;
    itr = targetSpec.m_props.find("source");
    if (itr != targetSpec.m_props.end()) {
        source = itr->second;
    }

    // optional service
    std::string service;
    itr = targetSpec.m_props.find("service");
    if (itr != targetSpec.m_props.end()) {
        service = itr->second;
    }

    // optional tags
    std::string tags;
    itr = targetSpec.m_props.find("tags");
    if (itr != targetSpec.m_props.end()) {
        tags = itr->second;
    }

    // optional stack_trace
    bool stackTrace = false;
    itr = targetSpec.m_props.find("stack_trace");
    if (itr != targetSpec.m_props.end()) {
        if (!parseBoolProp("stack_trace", logTargetCfg, itr->second, stackTrace)) {
            ELOG_REPORT_ERROR(
                "Invalid Datadog log target specification, failed to parse boolean property "
                "stack_trace '%s': %s",
                itr->second.c_str(), logTargetCfg.c_str());
            return nullptr;
        }
    }
    if (stackTrace) {
#ifndef ELOG_ENABLE_STACK_TRACE
        // TODO: should this be an error or a warning?
        ELOG_REPORT_ERROR(
            "Invalid Datadog log target specification. Unable to collect stack trace for Datadog "
            "log target because ELog was not built with stack trace support (requires "
            "ELOG_ENABLE_STACK_TRACE=ON).");
        return nullptr;
#endif
    }

    // optional compress
    bool compress = false;
    itr = targetSpec.m_props.find("compress");
    if (itr != targetSpec.m_props.end()) {
        if (!parseBoolProp("compress", logTargetCfg, itr->second, compress)) {
            ELOG_REPORT_ERROR(
                "Invalid Datadog log target specification, failed to parse boolean property "
                "compress '%s': %s",
                itr->second.c_str(), logTargetCfg.c_str());
            return nullptr;
        }
    }

    // timeouts
    uint32_t connectTimeoutMillis = ELOG_DATADOG_DEFAULT_CONNECT_TIMEOUT_MILLIS;
    itr = targetSpec.m_props.find("connect_timeout_millis");
    if (itr != targetSpec.m_props.end()) {
        if (!parseIntProp("connect_timeout_millis", logTargetCfg, itr->second,
                          connectTimeoutMillis)) {
            ELOG_REPORT_ERROR(
                "Invalid Datadog log target specification, failed to parse connect timeout "
                "'%s': %s",
                itr->second.c_str(), logTargetCfg.c_str());
            return nullptr;
        }
    }

    uint32_t writeTimeoutMillis = ELOG_DATADOG_DEFAULT_WRITE_TIMEOUT_MILLIS;
    itr = targetSpec.m_props.find("write_timeout_millis");
    if (itr != targetSpec.m_props.end()) {
        if (!parseIntProp("write_timeout_millis", logTargetCfg, itr->second, writeTimeoutMillis)) {
            ELOG_REPORT_ERROR(
                "Invalid Datadog log target specification, failed to parse write timeout "
                "'%s': %s",
                itr->second.c_str(), logTargetCfg.c_str());
            return nullptr;
        }
    }

    uint32_t readTimeoutMillis = ELOG_DATADOG_DEFAULT_READ_TIMEOUT_MILLIS;
    itr = targetSpec.m_props.find("read_timeout_millis");
    if (itr != targetSpec.m_props.end()) {
        if (!parseIntProp("read_timeout_millis", logTargetCfg, itr->second, readTimeoutMillis)) {
            ELOG_REPORT_ERROR(
                "Invalid Datadog log target specification, failed to parse read timeout "
                "'%s': %s",
                itr->second.c_str(), logTargetCfg.c_str());
            return nullptr;
        }
    }

    ELogDatadogTarget* target = new (std::nothrow) ELogDatadogTarget(
        endpoint.c_str(), apiKey.c_str(), source.c_str(), service.c_str(), tags.c_str(), stackTrace,
        compress, connectTimeoutMillis, writeTimeoutMillis, readTimeoutMillis);
    if (target == nullptr) {
        ELOG_REPORT_ERROR("Failed to allocate Datadog log target, out of memory");
    }
    return target;
}

ELogMonTarget* ELogDatadogTargetProvider::loadTarget(const ELogConfigMapNode* logTargetCfg) {
    // expected url is as follows:
    // mon://datadog?
    //  endpoint=http://host:port&  // e.g. endpoint=https://http-intake.logs.datadoghq.com
    //  api_key=<key>&
    //  source=<name>&
    //  service=<name>&
    //  tags={JSON_FORMAT}&
    //  stack_trace=yes/no&
    //  compress=yes/no&
    //  connect_timeout_millis=value&
    //  write_timeout_millis=value&
    //  read_timeout_millis=value

    // we expect the following properties: endpoint (mandatory), tags, source, service, compress and
    // connect timeout, write timeout, aggregation may be controlled by flush policy tags is normal
    // field selector stuff
    std::string endpoint;
    if (!ELogConfigLoader::getLogTargetStringProperty(logTargetCfg, "Datadog", "endpoint",
                                                      endpoint)) {
        return nullptr;
    }

    std::string apiKey;
    if (!ELogConfigLoader::getLogTargetStringProperty(logTargetCfg, "Datadog", "api_key", apiKey)) {
        return nullptr;
    }

    // optional source
    std::string source;
    if (!ELogConfigLoader::getOptionalLogTargetStringProperty(logTargetCfg, "Datadog", "source",
                                                              source)) {
        return nullptr;
    }

    // optional service
    std::string service;
    if (!ELogConfigLoader::getOptionalLogTargetStringProperty(logTargetCfg, "Datadog", "service",
                                                              service)) {
        return nullptr;
    }

    // optional tags
    std::string tags;
    if (!ELogConfigLoader::getOptionalLogTargetStringProperty(logTargetCfg, "Datadog", "tags",
                                                              tags)) {
        return nullptr;
    }

    // optional stack_trace
    bool stackTrace = false;
    if (!ELogConfigLoader::getOptionalLogTargetBoolProperty(logTargetCfg, "Datadog", "stack_trace",
                                                            stackTrace)) {
        return nullptr;
    }
    if (stackTrace) {
#ifndef ELOG_ENABLE_STACK_TRACE
        // TODO: should this be an error or a warning?
        ELOG_REPORT_ERROR(
            "Invalid Datadog log target specification. Unable to collect stack trace for Datadog "
            "log target because ELog was not built with stack trace support (requires "
            "ELOG_ENABLE_STACK_TRACE=ON).");
        return nullptr;
#endif
    }

    // optional compress
    bool compress = false;
    if (!ELogConfigLoader::getOptionalLogTargetBoolProperty(logTargetCfg, "Datadog", "compress",
                                                            compress)) {
        return nullptr;
    }

    // timeouts
    int64_t connectTimeoutMillis = ELOG_DATADOG_DEFAULT_CONNECT_TIMEOUT_MILLIS;
    if (!ELogConfigLoader::getOptionalLogTargetIntProperty(
            logTargetCfg, "Datadog", "connect_timeout_millis", connectTimeoutMillis)) {
        return nullptr;
    }

    int64_t writeTimeoutMillis = ELOG_DATADOG_DEFAULT_WRITE_TIMEOUT_MILLIS;
    if (!ELogConfigLoader::getOptionalLogTargetIntProperty(
            logTargetCfg, "Datadog", "write_timeout_millis", writeTimeoutMillis)) {
        return nullptr;
    }

    int64_t readTimeoutMillis = ELOG_DATADOG_DEFAULT_READ_TIMEOUT_MILLIS;
    if (!ELogConfigLoader::getOptionalLogTargetIntProperty(
            logTargetCfg, "Datadog", "read_timeout_millis", readTimeoutMillis)) {
        return nullptr;
    }

    ELogDatadogTarget* target = new (std::nothrow) ELogDatadogTarget(
        endpoint.c_str(), apiKey.c_str(), source.c_str(), service.c_str(), tags.c_str(), stackTrace,
        compress, connectTimeoutMillis, writeTimeoutMillis, readTimeoutMillis);
    if (target == nullptr) {
        ELOG_REPORT_ERROR("Failed to allocate Datadog log target, out of memory");
    }
    return target;
}

}  // namespace elog

#endif  // ELOG_ENABLE_DATADOG_CONNECTOR
