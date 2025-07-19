#include "elog_sentry_target_provider.h"

#ifdef ELOG_ENABLE_SENTRY_CONNECTOR

#include "elog_common.h"
#include "elog_config_loader.h"
#include "elog_error.h"
#include "elog_sentry_target.h"

namespace elog {

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
    //  attributes={<key-value list, comma-separated>}
    //  stack_trace=yes/no
    //  mode=message/logs

    ELogSentryParams params;

    // first check for env var SENTRY_DSN
    if (elog_getenv("SENTRY_DSN", params.m_dsn)) {
        // do not print key and cause security breach
        ELOG_REPORT_TRACE("Using ENTRY_DSN environment variable");
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

    // optional attributes
    if (!ELogConfigLoader::getOptionalLogTargetStringProperty(logTargetCfg, "Sentry", "attributes",
                                                              params.m_attributes)) {
        return nullptr;
    }

    // optional mode
    std::string mode = "message";
    if (!ELogConfigLoader::getOptionalLogTargetStringProperty(logTargetCfg, "Sentry", "mode",
                                                              mode)) {
        return nullptr;
    }
    if (mode.compare("message") == 0) {
        params.m_mode = ELogSentryParams::Mode::MODE_MESSAGE;
    } else if (mode.compare("logs") == 0) {
        params.m_mode = ELogSentryParams::Mode::MODE_LOGS;
        ELOG_REPORT_WARN(
            "Sentry log target 'logs' report mode is not supported yet (waiting for native SDK "
            "support). In the meantime 'message' mode will be used.");
        params.m_mode = ELogSentryParams::Mode::MODE_MESSAGE;
    } else {
        ELOG_REPORT_ERROR(
            "Invalid Sentry log target specification, mode can be only 'message' or 'logs'");
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
    uint64_t timeoutMillis = ELOG_SENTRY_DEFAULT_FLUSH_TIMEOUT_MILLIS;
    if (!ELogConfigLoader::getOptionalLogTargetUIntProperty(
            logTargetCfg, "Sentry", "flush_timeout_millis", timeoutMillis)) {
        return nullptr;
    }
    params.m_flushTimeoutMillis = timeoutMillis;

    // optional shutdown timeout
    timeoutMillis = ELOG_SENTRY_DEFAULT_SHUTDOWN_TIMEOUT_MILLIS;
    if (!ELogConfigLoader::getOptionalLogTargetUIntProperty(
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
