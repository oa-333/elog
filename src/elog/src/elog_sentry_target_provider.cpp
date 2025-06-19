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

    ELogPropertyMap::const_iterator itr = targetSpec.m_props.find("dsn");
    if (itr == targetSpec.m_props.end()) {
        ELOG_REPORT_ERROR("Invalid Sentry log target specification, missing property 'dsn': %s",
                          logTargetCfg.c_str());
        return nullptr;
    }
    const std::string& dsn = itr->second;

    itr = targetSpec.m_props.find("db_path");
    if (itr == targetSpec.m_props.end()) {
        ELOG_REPORT_ERROR("Invalid Sentry log target specification, missing property 'db_path': %s",
                          logTargetCfg.c_str());
        return nullptr;
    }
    const std::string& dbPath = itr->second;

    itr = targetSpec.m_props.find("release");
    if (itr == targetSpec.m_props.end()) {
        ELOG_REPORT_ERROR("Invalid Sentry log target specification, missing property 'release': %s",
                          logTargetCfg.c_str());
        return nullptr;
    }
    const std::string& release = itr->second;

    itr = targetSpec.m_props.find("env");
    if (itr == targetSpec.m_props.end()) {
        ELOG_REPORT_ERROR("Invalid Sentry log target specification, missing property 'env': %s",
                          logTargetCfg.c_str());
        return nullptr;
    }
    const std::string& env = itr->second;

    // optional dist, ca_certs_path, proxy
    std::string dist;
    itr = targetSpec.m_props.find("dist");
    if (itr != targetSpec.m_props.end()) {
        dist = itr->second;
    }
    std::string caCertsPath;
    itr = targetSpec.m_props.find("ca_certs_path");
    if (itr != targetSpec.m_props.end()) {
        caCertsPath = itr->second;
    }
    std::string proxy;
    itr = targetSpec.m_props.find("proxy");
    if (itr != targetSpec.m_props.end()) {
        proxy = itr->second;
    }

    // optional timeouts
    uint32_t flushTimeoutMillis = ELOG_SENTRY_DEFAULT_FLUSH_TIMEOUT_MILLIS;
    itr = targetSpec.m_props.find("flush_timeout_millis");
    if (itr != targetSpec.m_props.end()) {
        if (!parseIntProp("flush_timeout_millis", logTargetCfg, itr->second, flushTimeoutMillis)) {
            ELOG_REPORT_ERROR(
                "Invalid Sentry log target specification, failed to parse flush timeout '%s': %s",
                itr->second.c_str(), logTargetCfg.c_str());
            return nullptr;
        }
    }

    uint32_t shutdownTimeoutMillis = ELOG_SENTRY_DEFAULT_FLUSH_TIMEOUT_MILLIS;
    itr = targetSpec.m_props.find("write_timeout_millis");
    if (itr != targetSpec.m_props.end()) {
        if (!parseIntProp("shutdown_timeout_millis", logTargetCfg, itr->second,
                          shutdownTimeoutMillis)) {
            ELOG_REPORT_ERROR(
                "Invalid Sentry log target specification, failed to parse shutdown timeout '%s': "
                "%s",
                itr->second.c_str(), logTargetCfg.c_str());
            return nullptr;
        }
    }

    // optional debug
    bool debug = false;
    itr = targetSpec.m_props.find("debug");
    if (itr != targetSpec.m_props.end()) {
        if (!parseBoolProp("debug", logTargetCfg, itr->second, debug)) {
            ELOG_REPORT_ERROR(
                "Invalid Sentry log target specification, failed to parse debug property '%s': %s",
                itr->second.c_str(), logTargetCfg.c_str());
            return nullptr;
        }
    }

    ELogSentryTarget* target = new (std::nothrow) ELogSentryTarget(
        dsn.c_str(), dbPath.c_str(), release.c_str(), env.c_str(), dist.c_str(),
        caCertsPath.c_str(), proxy.c_str(), flushTimeoutMillis, shutdownTimeoutMillis, debug);
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
    //  flush_timeout_millis=value
    //  shutdown_timeout_millis=value
    //  debug=yes/no

    std::string dsn;
    if (!ELogConfigLoader::getLogTargetStringProperty(logTargetCfg, "Sentry", "dsn", dsn)) {
        return nullptr;
    }

    std::string dbPath;
    if (!ELogConfigLoader::getLogTargetStringProperty(logTargetCfg, "Sentry", "db_path", dbPath)) {
        return nullptr;
    }

    std::string release;
    if (!ELogConfigLoader::getLogTargetStringProperty(logTargetCfg, "Sentry", "release", release)) {
        return nullptr;
    }

    std::string env;
    if (!ELogConfigLoader::getLogTargetStringProperty(logTargetCfg, "Sentry", "env", env)) {
        return nullptr;
    }

    std::string dist;
    if (!ELogConfigLoader::getLogTargetStringProperty(logTargetCfg, "Sentry", "dist", dist)) {
        return nullptr;
    }

    // optional log line metadata
    std::string caCertsPath;
    if (!ELogConfigLoader::getOptionalLogTargetStringProperty(logTargetCfg, "Sentry",
                                                              "ca_certs_path", caCertsPath)) {
        return nullptr;
    }

    // optional proxy
    std::string proxy;
    if (!ELogConfigLoader::getOptionalLogTargetStringProperty(logTargetCfg, "Sentry", "proxy",
                                                              proxy)) {
        return nullptr;
    }

    // timeouts
    int64_t flushTimeoutMillis = ELOG_SENTRY_DEFAULT_FLUSH_TIMEOUT_MILLIS;
    if (!ELogConfigLoader::getOptionalLogTargetIntProperty(
            logTargetCfg, "Sentry", "flush_timeout_millis", flushTimeoutMillis)) {
        return nullptr;
    }

    int64_t shutdownTimeoutMillis = ELOG_SENTRY_DEFAULT_SHUTDOWN_TIMEOUT_MILLIS;
    if (!ELogConfigLoader::getOptionalLogTargetIntProperty(
            logTargetCfg, "Sentry", "shutdown_timeout_millis", shutdownTimeoutMillis)) {
        return nullptr;
    }

    // debug
    bool debug = false;
    if (!ELogConfigLoader::getOptionalLogTargetBoolProperty(logTargetCfg, "Sentry", "debug",
                                                            debug)) {
        return nullptr;
    }

    ELogSentryTarget* target = new (std::nothrow) ELogSentryTarget(
        dsn.c_str(), dbPath.c_str(), release.c_str(), env.c_str(), dist.c_str(),
        caCertsPath.c_str(), proxy.c_str(), flushTimeoutMillis, shutdownTimeoutMillis, debug);
    if (target == nullptr) {
        ELOG_REPORT_ERROR("Failed to allocate Sentry log target, out of memory");
    }
    return target;
}

}  // namespace elog

#endif  // ELOG_ENABLE_GRAFANA_CONNECTOR
