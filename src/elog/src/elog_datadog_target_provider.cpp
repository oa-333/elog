#include "elog_datadog_target_provider.h"

#ifdef ELOG_ENABLE_DATADOG_CONNECTOR

#include "elog_common.h"
#include "elog_config_loader.h"
#include "elog_datadog_target.h"
#include "elog_http_config_loader.h"
#include "elog_report.h"

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogDatadogTargetProvider)

ELogMonTarget* ELogDatadogTargetProvider::loadTarget(const ELogConfigMapNode* logTargetCfg) {
    // expected url is as follows:
    // mon://datadog?
    //  address=http://host:port&  // e.g. address=https://http-intake.logs.datadoghq.com
    //  api_key=<key>&
    //  source=<name>&
    //  service=<name>&
    //  tags={JSON_FORMAT}&
    //  stack_trace=yes/no&
    //  compress=yes/no&
    //  connect_timeout=value&
    //  write_timeout=value&
    //  read_timeout=value
    //  resend_period=value&
    //  backlog_limit=value&
    //  shutdown_timeout=value

    // we expect the following properties: address (mandatory), tags, source, service, compress and
    // connect timeout, write timeout, aggregation may be controlled by flush policy tags is normal
    // field selector stuff
    std::string address;
    if (!ELogConfigLoader::getLogTargetStringProperty(logTargetCfg, "Datadog", "address",
                                                      address)) {
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

    // load common HTTP configuration (use defaults, see ELogHttpConfig default constructor)
    ELogHttpConfig httpConfig;
    if (!ELogHttpConfigLoader::loadHttpConfig(logTargetCfg, "Datadog", httpConfig)) {
        ELOG_REPORT_ERROR(
            "Invalid Datadog log target specification, invalid HTTP properties (context: %s)",
            logTargetCfg->getFullContext());
        return nullptr;
    }

    ELogDatadogTarget* target = new (std::nothrow)
        ELogDatadogTarget(address.c_str(), apiKey.c_str(), httpConfig, source.c_str(),
                          service.c_str(), tags.c_str(), stackTrace, compress);
    if (target == nullptr) {
        ELOG_REPORT_ERROR("Failed to allocate Datadog log target, out of memory");
    }
    return target;
}

}  // namespace elog

#endif  // ELOG_ENABLE_DATADOG_CONNECTOR
