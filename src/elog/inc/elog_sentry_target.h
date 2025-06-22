#ifndef __ELOG_SENTRY_TARGET_H__
#define __ELOG_SENTRY_TARGET_H__

#include "elog_def.h"

#ifdef ELOG_ENABLE_SENTRY_CONNECTOR

#include "elog_mon_target.h"
#include "elog_props_formatter.h"

#define ELOG_SENTRY_DEFAULT_FLUSH_TIMEOUT_MILLIS 1000
#define ELOG_SENTRY_DEFAULT_SHUTDOWN_TIMEOUT_MILLIS 5000

namespace elog {

struct ELOG_API ELogSentryParams {
    std::string m_dsn;
    std::string m_dbPath;
    std::string m_releaseName;
    std::string m_env;
    std::string m_dist;
    std::string m_caCertsPath;
    std::string m_proxy;
    std::string m_handlerPath;
    std::string m_context;
    std::string m_contextTitle;
    std::string m_tags;
    std::string m_attributes;
    enum class Mode : uint32_t { MODE_MESSAGE, MODE_LOGS } m_mode;
    bool m_stackTrace;
    uint64_t m_flushTimeoutMillis;
    uint64_t m_shutdownTimeoutMillis;
    bool m_debug;
    std::string m_loggerLevel;
};

class ELOG_API ELogSentryTarget : public ELogMonTarget {
public:
    ELogSentryTarget(const ELogSentryParams& params) : m_params(params) {}

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

    ELogSentryParams m_params;
    ELogPropsFormatter m_contextFormatter;
    ELogPropsFormatter m_tagsFormatter;
    ELogPropsFormatter m_attributesFormatter;
};

}  // namespace elog

#endif  // ELOG_ENABLE_SENTRY_CONNECTOR

#endif  // __ELOG_SENTRY_TARGET_H__