#ifndef __ELOG_SENTRY_TARGET_H__
#define __ELOG_SENTRY_TARGET_H__

#include "elog_def.h"

#define ELOG_ENABLE_SENTRY_CONNECTOR
#ifdef ELOG_ENABLE_SENTRY_CONNECTOR

#include "elog_mon_target.h"

#define ELOG_SENTRY_DEFAULT_FLUSH_TIMEOUT_MILLIS 1000
#define ELOG_SENTRY_DEFAULT_SHUTDOWN_TIMEOUT_MILLIS 5000

namespace elog {

class ELOG_API ELogSentryTarget : public ELogMonTarget {
public:
    ELogSentryTarget(const char* dsn, const char* dbPath, const char* releaseName, const char* env,
                     const char* dist = "", const char* caCertsPath = "", const char* proxy = "",
                     const char* handlerPath = "",
                     uint64_t flushTimeoutMillis = ELOG_SENTRY_DEFAULT_FLUSH_TIMEOUT_MILLIS,
                     uint64_t shutdownTimeoutMillis = ELOG_SENTRY_DEFAULT_SHUTDOWN_TIMEOUT_MILLIS,
                     bool debug = false, const char* loggerLevel = "")
        : m_dsn(dsn),
          m_dbPath(dbPath),
          m_releaseName(releaseName),
          m_env(env),
          m_dist(dist),
          m_caCertsPath(caCertsPath),
          m_proxy(proxy),
          m_handlerPath(handlerPath),
          m_flushTimeoutMillis(flushTimeoutMillis),
          m_shutdownTimeoutMillis(shutdownTimeoutMillis),
          m_debug(debug),
          m_loggerLevel(loggerLevel) {}

    ELogSentryTarget(const ELogSentryTarget&) = delete;
    ELogSentryTarget(ELogSentryTarget&&) = delete;
    ~ELogSentryTarget() override {}

protected:
    /** @brief Order the log target to start (required for threaded targets). */
    bool startLogTarget() override;

    /** @brief Order the log target to stop (required for threaded targets). */
    bool stopLogTarget() override;

    /**
     * @brief Order the log target to write a log record (thread-safe).
     * @return The number of bytes written to log.
     */
    uint32_t writeLogRecord(const ELogRecord& logRecord) override;

    /** @brief Order the log target to flush. */
    void flushLogTarget() override;

    std::string m_dsn;
    std::string m_dbPath;
    std::string m_releaseName;
    std::string m_env;
    std::string m_dist;
    std::string m_caCertsPath;
    std::string m_proxy;
    std::string m_handlerPath;
    uint64_t m_flushTimeoutMillis;
    uint64_t m_shutdownTimeoutMillis;
    bool m_debug;
    std::string m_loggerLevel;
};

}  // namespace elog

#endif  // ELOG_ENABLE_SENTRY_CONNECTOR

#endif  // __ELOG_SENTRY_TARGET_H__