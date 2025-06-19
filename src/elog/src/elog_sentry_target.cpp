#include "elog_sentry_target.h"

#ifdef ELOG_ENABLE_SENTRY_CONNECTOR

#include <sentry.h>

#include "elog_error.h"
#include "elog_logger.h"

namespace elog {

// TODO: we can report thread id-name mappings to sentry, for this we need notification mechanism
// the relevant sentry API is sentry_value_new_thread
// we can also report stack traces to sentry, this also requires notification mechanism
// the relevant sentry API is sentry_value_new_stacktrace()/sentry_value_set_stacktrace() but
// apparently Sentry lacks the ability to resolve symbols (can contact them to add extra interface)

// TODO: understand how to work with Sentry transactions

inline sentry_level_e elogLevelToSentryLevel(ELogLevel logLevel) {
    switch (logLevel) {
        case ELEVEL_FATAL:
            return SENTRY_LEVEL_FATAL;
        case ELEVEL_ERROR:
            return SENTRY_LEVEL_ERROR;
        case ELEVEL_WARN:
            return SENTRY_LEVEL_WARNING;
        case ELEVEL_NOTICE:
        case ELEVEL_INFO:
            return SENTRY_LEVEL_INFO;
        default:
            return SENTRY_LEVEL_DEBUG;
    }
}

bool ELogSentryTarget::startLogTarget() {
    sentry_options_t* options = sentry_options_new();
    sentry_options_set_dsn(options, m_dsn.c_str());
    if (!m_dbPath.empty()) {
        sentry_options_set_database_path(options, ".sentry-native");
    }
    if (!m_releaseName.empty()) {
        sentry_options_set_release(options, m_releaseName.c_str());
    }
    if (!m_env.empty()) {
        sentry_options_set_environment(options, m_env.c_str());
    }
    if (!m_dist.empty()) {
        sentry_options_set_dist(options, m_dist.c_str());
    }
    if (!m_caCertsPath.empty()) {
        sentry_options_set_ca_certs(options, m_caCertsPath.c_str());
    }
    if (!m_proxy.empty()) {
        sentry_options_set_proxy(options, m_proxy.c_str());
    }
    sentry_options_set_shutdown_timeout(options, m_shutdownTimeoutMillis);
    sentry_options_set_debug(options, m_debug ? 1 : 0);
    sentry_init(options);

    // more interesting options (currently not supported):
    // sentry_options_set_logger - capture sentry internal log messages (debug only)
    // sentry_options_set_logger_level - configure sentry logger level (debug only)

    return true;
}

bool ELogSentryTarget::stopLogTarget() {
    sentry_close();
    return true;
}

uint32_t ELogSentryTarget::writeLogRecord(const ELogRecord& logRecord) {
    std::string logMsg;
    formatLogMsg(logRecord, logMsg);
    sentry_capture_event(sentry_value_new_message_event(
        /*   level */ elogLevelToSentryLevel(logRecord.m_logLevel),
        /*  logger */ logRecord.m_logger->getLogSource()->getQualifiedName(),
        /* message */ logMsg.c_str()));
    return logMsg.length();
}

void ELogSentryTarget::flushLogTarget() {
    int res = sentry_flush(m_flushTimeoutMillis);
    if (res != 0) {
        ELOG_REPORT_ERROR("Failed to flush Sentry transport");
    }
}

}  // namespace elog

#endif  // ELOG_ENABLE_GRAFANA_CONNECTOR