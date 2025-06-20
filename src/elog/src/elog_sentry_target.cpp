#include "elog_sentry_target.h"

#ifdef ELOG_ENABLE_SENTRY_CONNECTOR

#include <sentry.h>

#include "elog_error.h"
#include "elog_logger.h"
#include "elog_system.h"

namespace elog {

// TODO: we can report thread id-name mappings to sentry, for this we need notification mechanism
// the relevant sentry API is sentry_value_new_thread
// we can also report stack traces to sentry, this also requires notification mechanism
// the relevant sentry API is sentry_value_new_stacktrace()/sentry_value_set_stacktrace() but
// apparently Sentry lacks the ability to resolve symbols (can contact them to add extra interface)

// TODO: understand how to work with Sentry transactions

// TODO: if there is a SENTRY_DSN env var then use it (override config, also allows empty config)

// TODO: Sentry allows attaching stack trace to events, so we should check how to do that, and allow
// user on config to specify so

// TODO: checkout the beta logging interface

// TODO: learn how to use envelopes and transport

static ELogLogger* sSentryLogger = nullptr;

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

inline ELogLevel sentryLogLevelToELog(sentry_level_t logLevel) {
    switch (logLevel) {
        case SENTRY_LEVEL_FATAL:
            return ELEVEL_FATAL;
        case SENTRY_LEVEL_ERROR:
            return ELEVEL_ERROR;
        case SENTRY_LEVEL_WARNING:
            return ELEVEL_WARN;
        case SENTRY_LEVEL_INFO:
            return ELEVEL_INFO;
        case SENTRY_LEVEL_DEBUG:
            return ELEVEL_DEBUG;
        default:
            return ELEVEL_INFO;
    }
}

static void sentryLoggerFunc(sentry_level_t level, const char* message, va_list args,
                             void* userdata);

bool ELogSentryTarget::startLogTarget() {
    sentry_options_t* options = sentry_options_new();
    sentry_options_set_dsn(options, m_dsn.c_str());
    if (!m_dbPath.empty()) {
        sentry_options_set_database_path(options, m_dbPath.c_str());
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
    // if user built Sentry with launch-pad backend, then he should also provide handler path, so we
    // should allow it from configuration (optional)
    if (!m_handlerPath.empty()) {
        sentry_options_set_handler_path(options, m_handlerPath.c_str());
    }

    sentry_options_set_shutdown_timeout(options, m_shutdownTimeoutMillis);
    sentry_options_set_debug(options, m_debug ? 1 : 0);

    // finally configure sentry logger (only if debug is set)
    if (m_debug) {
        ELogSource* logSource = ELogSystem::defineLogSource("elog.sentry", true);
        if (logSource != nullptr) {
            // make sure we do not enter infinite loop, so we make sentry log source writes only to
            // stderr, this requires stderr to be defined BEFORE sentry log target
            ELogTargetAffinityMask mask = 0;
            ELogTargetId logTargetId = ELogSystem::getLogTargetId("stderr");
            if (logTargetId != ELOG_INVALID_TARGET_ID) {
                ELOG_ADD_TARGET_AFFINITY_MASK(mask, logTargetId);
                logSource->setLogTargetAffinity(mask);
            } else {
                ELOG_REPORT_WARN(
                    "Could not restrict sentry log source to stderr (stderr log target not found)");
            }
            sSentryLogger = logSource->createSharedLogger();
        } else {
            ELOG_REPORT_WARN("Sentry logger could not be set up, failed to define log source");
        }
        sentry_options_set_logger(options, sentryLoggerFunc, nullptr);

        // allow user to control log level
        if (!m_loggerLevel.empty()) {
            ELogLevel level = ELEVEL_INFO;
            if (!elogLevelFromStr(m_loggerLevel.c_str(), level)) {
                ELOG_REPORT_WARN("Invalid logger level '%s' for Sentry logger, using INFO instead");
                level = ELEVEL_INFO;
            }
            sentry_options_set_logger_level(options, elogLevelToSentryLevel(level));
        }
    }
    sentry_init(options);

    return true;
}

bool ELogSentryTarget::stopLogTarget() {
    // NOTE: we get a deadlock here because sentry_close() outputs a debug message, but stderr log
    // target may have already been deleted by now.
    // first we note that setting sSentryLogger to null before calling sentry_close() is not enough,
    // since during the time window since stderr had been deleted, until the time we got here, there
    // may be more messages issued by sentry logger, which will lead to crash/deadlock

    // we can handle this in several ways:
    // - the best way would be to get notification "log target stopped", so when stderr goes does,
    // and before it gets deleted, we can nullify the sentry logger (and then write directly to
    // stderr or whatever)
    // - we can also register a "log target started", so we can restrict affinity to stderr log
    // target when it gets added, and also register for its removal event
    // - we can avoid restricting ourselves to stderr log target altogether, and instead, we just
    // make sure we don't send logs to ourselves. for this to happen we must get the log target id
    // of the sentry log target, but during start it is not present yet. If we modify log target to
    // first grab a slot (lock free), and then call start (and if failed release slot), then this
    // would work, we only need to put the target id in the target, so we don't need to guess what
    // log target name the user used
    // the last solution is the best, since it does not require dependency on a specific log target
    // (what if it is not defined, or defined not early enough?), but it requires more development
    // effort, which is not that much

    // for now we will just set it to null
    sSentryLogger = nullptr;
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

void sentryLoggerFunc(sentry_level_t level, const char* message, va_list args, void* userdata) {
    if (sSentryLogger != nullptr) {
        sSentryLogger->logFormatV(sentryLogLevelToELog(level), "", 0, "", message, args);
    } else {
        vfprintf(stderr, message, args);
    }
}

}  // namespace elog

#endif  // ELOG_ENABLE_GRAFANA_CONNECTOR