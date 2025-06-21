#include "elog_sentry_target_provider.h"

#ifdef ELOG_ENABLE_SENTRY_CONNECTOR

#include "elog_common.h"
#include "elog_config_loader.h"
#include "elog_error.h"
#include "elog_sentry_target.h"

namespace elog {

ELogMonTarget* ELogSentryTargetProvider::loadTarget(const std::string& logTargetCfg,
                                                    const ELogTargetSpec& targetSpec) {
    // expected url is as follows:
    // mon://sentry?
    //  dsn=https://examplePublicKey@o0.ingest.sentry.io/0&
    //  db_path=<path>&
    //  release=my-project-name@2.3.12&
    //  env=staging&
    //  dist=<name>&
    //  ca_certs_path=<file-path>&
    //  proxy=https://host:port&
    //  flush_timeout_millis=value
    //  shutdown_timeout_millis=value
    //  debug=yes/no

    ELogSentryParams params;
    ELogPropertyMap::const_iterator itr = targetSpec.m_props.find("dsn");
    if (itr == targetSpec.m_props.end()) {
        ELOG_REPORT_ERROR("Invalid Sentry log target specification, missing property 'dsn': %s",
                          logTargetCfg.c_str());
        return nullptr;
    }
    params.m_dsn = itr->second;

    itr = targetSpec.m_props.find("db_path");
    if (itr == targetSpec.m_props.end()) {
        ELOG_REPORT_ERROR("Invalid Sentry log target specification, missing property 'db_path': %s",
                          logTargetCfg.c_str());
        return nullptr;
    }
    params.m_dbPath = itr->second;

    itr = targetSpec.m_props.find("release");
    if (itr == targetSpec.m_props.end()) {
        ELOG_REPORT_ERROR("Invalid Sentry log target specification, missing property 'release': %s",
                          logTargetCfg.c_str());
        return nullptr;
    }
    params.m_releaseName = itr->second;

    itr = targetSpec.m_props.find("env");
    if (itr == targetSpec.m_props.end()) {
        ELOG_REPORT_ERROR("Invalid Sentry log target specification, missing property 'env': %s",
                          logTargetCfg.c_str());
        return nullptr;
    }
    params.m_env = itr->second;

    // optional dist, ca_certs_path, proxy
    itr = targetSpec.m_props.find("dist");
    if (itr != targetSpec.m_props.end()) {
        params.m_dist = itr->second;
    }
    itr = targetSpec.m_props.find("ca_certs_path");
    if (itr != targetSpec.m_props.end()) {
        params.m_caCertsPath = itr->second;
    }
    itr = targetSpec.m_props.find("proxy");
    if (itr != targetSpec.m_props.end()) {
        params.m_proxy = itr->second;
    }
    itr = targetSpec.m_props.find("handler_path");
    if (itr != targetSpec.m_props.end()) {
        params.m_handlerPath = itr->second;
    }
    itr = targetSpec.m_props.find("context");
    if (itr != targetSpec.m_props.end()) {
        params.m_context = itr->second;
    }
    itr = targetSpec.m_props.find("context_title");
    if (itr != targetSpec.m_props.end()) {
        params.m_contextTitle = itr->second;
    }
    itr = targetSpec.m_props.find("tags");
    if (itr != targetSpec.m_props.end()) {
        params.m_tags = itr->second;
    }

    // optional timeouts
    params.m_flushTimeoutMillis = ELOG_SENTRY_DEFAULT_FLUSH_TIMEOUT_MILLIS;
    itr = targetSpec.m_props.find("flush_timeout_millis");
    if (itr != targetSpec.m_props.end()) {
        if (!parseIntProp("flush_timeout_millis", logTargetCfg, itr->second,
                          params.m_flushTimeoutMillis)) {
            ELOG_REPORT_ERROR(
                "Invalid Sentry log target specification, failed to parse flush timeout '%s': %s",
                itr->second.c_str(), logTargetCfg.c_str());
            return nullptr;
        }
    }

    params.m_shutdownTimeoutMillis = ELOG_SENTRY_DEFAULT_FLUSH_TIMEOUT_MILLIS;
    itr = targetSpec.m_props.find("write_timeout_millis");
    if (itr != targetSpec.m_props.end()) {
        if (!parseIntProp("shutdown_timeout_millis", logTargetCfg, itr->second,
                          params.m_shutdownTimeoutMillis)) {
            ELOG_REPORT_ERROR(
                "Invalid Sentry log target specification, failed to parse shutdown timeout '%s': "
                "%s",
                itr->second.c_str(), logTargetCfg.c_str());
            return nullptr;
        }
    }

    // optional debug
    itr = targetSpec.m_props.find("debug");
    if (itr != targetSpec.m_props.end()) {
        if (!parseBoolProp("debug", logTargetCfg, itr->second, params.m_debug)) {
            ELOG_REPORT_ERROR(
                "Invalid Sentry log target specification, failed to parse debug property '%s': %s",
                itr->second.c_str(), logTargetCfg.c_str());
            return nullptr;
        }
    }

    itr = targetSpec.m_props.find("loggerLevel");
    if (itr != targetSpec.m_props.end()) {
        params.m_loggerLevel = itr->second;
    }

    ELogSentryTarget* target = new (std::nothrow) ELogSentryTarget(params);
    if (target == nullptr) {
        ELOG_REPORT_ERROR("Failed to allocate Sentry log target, out of memory");
    }
    return target;
}

ELogMonTarget* ELogSentryTargetProvider::loadTarget(const ELogConfigMapNode* logTargetCfg) {
    // expected url is as follows:
    // mon://sentry?
    //  dsn=https://examplePublicKey@o0.ingest.sentry.io/0&
    //  db_path=<path>&
    //  release=my-project-name@2.3.12&
    //  env=staging&
    //  dist=<name>&
    //  ca_certs_path=<file-path>&
    //  proxy=https://host:port&
    //  handler_path=<path>
    //  flush_timeout_millis=value
    //  shutdown_timeout_millis=value
    //  debug=yes/no
    //  logger_level=FATAL/ERROR/WARN/INFO/DEBUG
    //  context={<key-value list, comma-separated>}
    //  tags={<key-value list, comma-separated>}
    //  stack_trace=yes/no

    ELogSentryParams params;

    // first check for env var SENTRY_DSN
    if (getenv("SENTRY_DSN") != nullptr) {
        params.m_dsn = getenv("SENTRY_DSN");
    } else if (!ELogConfigLoader::getLogTargetStringProperty(logTargetCfg, "Sentry", "dsn",
                                                             params.m_dsn)) {
        return nullptr;
    }

    if (!ELogConfigLoader::getLogTargetStringProperty(logTargetCfg, "Sentry", "db_path",
                                                      params.m_dbPath)) {
        return nullptr;
    }

    if (!ELogConfigLoader::getLogTargetStringProperty(logTargetCfg, "Sentry", "release",
                                                      params.m_releaseName)) {
        return nullptr;
    }

    if (!ELogConfigLoader::getLogTargetStringProperty(logTargetCfg, "Sentry", "env",
                                                      params.m_env)) {
        return nullptr;
    }

    // optional distribution
    if (!ELogConfigLoader::getOptionalLogTargetStringProperty(logTargetCfg, "Sentry", "dist",
                                                              params.m_dist)) {
        return nullptr;
    }

    // optional certificates path
    if (!ELogConfigLoader::getOptionalLogTargetStringProperty(
            logTargetCfg, "Sentry", "ca_certs_path", params.m_caCertsPath)) {
        return nullptr;
    }

    // optional proxy
    if (!ELogConfigLoader::getOptionalLogTargetStringProperty(logTargetCfg, "Sentry", "proxy",
                                                              params.m_proxy)) {
        return nullptr;
    }

    // optional handler path
    if (!ELogConfigLoader::getOptionalLogTargetStringProperty(
            logTargetCfg, "Sentry", "handler_path", params.m_handlerPath)) {
        return nullptr;
    }

    // optional context
    if (!ELogConfigLoader::getOptionalLogTargetStringProperty(logTargetCfg, "Sentry", "context",
                                                              params.m_context)) {
        return nullptr;
    }

    // optional context title
    if (!ELogConfigLoader::getOptionalLogTargetStringProperty(
            logTargetCfg, "Sentry", "context_title", params.m_contextTitle)) {
        return nullptr;
    }
    if (!params.m_context.empty() && params.m_contextTitle.empty()) {
        ELOG_REPORT_ERROR(
            "Invalid Sentry log target specification, when specifying 'context' property, "
            "'context_title' property must also be specified");
        return nullptr;
    }

    // optional tags
    if (!ELogConfigLoader::getOptionalLogTargetStringProperty(logTargetCfg, "Sentry", "tags",
                                                              params.m_tags)) {
        return nullptr;
    }

    // optional stack trace
    if (!ELogConfigLoader::getOptionalLogTargetBoolProperty(logTargetCfg, "Sentry", "stack_trace",
                                                            params.m_stackTrace)) {
        return nullptr;
    }
    if (params.m_stackTrace) {
#ifndef ELOG_ENABLE_STACK_TRACE
        // TODO: should this be an error or a warning?
        ELOG_REPORT_ERROR(
            "Invalid Sentry log target specification. Unable to collect stack trace for Sentry log "
            "target because ELog was not built with stack trace support (requires "
            "ELOG_ENABLE_STACK_TRACE=ON).");
        return nullptr;
#endif
    }

    // optional flush timeout
    bool found = false;
    int64_t timeoutMillis = ELOG_SENTRY_DEFAULT_FLUSH_TIMEOUT_MILLIS;
    if (!ELogConfigLoader::getOptionalLogTargetIntProperty(logTargetCfg, "Sentry",
                                                           "flush_timeout_millis", timeoutMillis)) {
        return nullptr;
    }
    params.m_flushTimeoutMillis = timeoutMillis;

    // optional shutdown timeout
    timeoutMillis = ELOG_SENTRY_DEFAULT_SHUTDOWN_TIMEOUT_MILLIS;
    if (!ELogConfigLoader::getOptionalLogTargetIntProperty(
            logTargetCfg, "Sentry", "shutdown_timeout_millis", timeoutMillis)) {
        return nullptr;
    }
    params.m_shutdownTimeoutMillis = timeoutMillis;

    // optional debug flag
    if (!ELogConfigLoader::getOptionalLogTargetBoolProperty(logTargetCfg, "Sentry", "debug",
                                                            params.m_debug)) {
        return nullptr;
    }

    // optional logger level
    if (!ELogConfigLoader::getOptionalLogTargetStringProperty(
            logTargetCfg, "Sentry", "logger_level", params.m_loggerLevel)) {
        return nullptr;
    }

    // create log target
    ELogSentryTarget* target = new (std::nothrow) ELogSentryTarget(params);
    if (target == nullptr) {
        ELOG_REPORT_ERROR("Failed to allocate Sentry log target, out of memory");
    }
    return target;
}

}  // namespace elog

#endif  // ELOG_ENABLE_GRAFANA_CONNECTOR
