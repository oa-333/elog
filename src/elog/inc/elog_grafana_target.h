#ifndef __ELOG_GRAFANA_TARGET_H__
#define __ELOG_GRAFANA_TARGET_H__

#include "elog_def.h"

#ifdef ELOG_ENABLE_GRAFANA_CONNECTOR

#include <httplib.h>

#include "elog_http_client.h"
#include "elog_mon_target.h"

#define ELOG_GRAFANA_DEFAULT_CONNECT_TIMEOUT_MILLIS 200
#define ELOG_GRAFANA_DEFAULT_WRITE_TIMEOUT_MILLIS 50
#define ELOG_GRAFANA_DEFAULT_READ_TIMEOUT_MILLIS 100

/** @def By default wait for 5 seconds before trying to resend failed HTTP messages. */
#define ELOG_GRAFANA_DEFAULT_RESEND_TIMEOUT_MILLIS 5000

/** @def By default allow for a total 1 MB of payload to be backlogged for resend. */
#define ELOG_GRAFANA_DEFAULT_BACKLOG_SIZE_BYTES (1024 * 1024)

/**
 * @def By default wait for 5 seconds before trying to resend failed HTTP messages during
 * shutdown.
 */
#define ELOG_GRAFANA_DEFAULT_SHUTDOWN_TIMEOUT_MILLIS 5000

namespace elog {

// both json and gRPC Grafana clients use HTTP client, so we factor out common code

class ELOG_API ELogGrafanaTarget : public ELogMonTarget, public ELogHttpResultHandler {
public:
    ELogGrafanaTarget(const char* lokiAddress,
                      uint32_t connectTimeoutMillis = ELOG_GRAFANA_DEFAULT_CONNECT_TIMEOUT_MILLIS,
                      uint32_t writeTimeoutMillis = ELOG_GRAFANA_DEFAULT_WRITE_TIMEOUT_MILLIS,
                      uint32_t readTimeoutMillis = ELOG_GRAFANA_DEFAULT_READ_TIMEOUT_MILLIS,
                      uint32_t resendPeriodMillis = ELOG_GRAFANA_DEFAULT_RESEND_TIMEOUT_MILLIS,
                      uint32_t backlogLimitBytes = ELOG_GRAFANA_DEFAULT_BACKLOG_SIZE_BYTES,
                      uint32_t shutdownTimeoutMillis = ELOG_GRAFANA_DEFAULT_SHUTDOWN_TIMEOUT_MILLIS)
        : m_lokiAddress(lokiAddress),
          m_connectTimeoutMillis(connectTimeoutMillis),
          m_writeTimeoutMillis(writeTimeoutMillis),
          m_readTimeoutMillis(readTimeoutMillis),
          m_resendPeriodMillis(resendPeriodMillis),
          m_backlogLimitBytes(backlogLimitBytes),
          m_shutdownTimeoutMillis(shutdownTimeoutMillis) {}

    ELogGrafanaTarget(const ELogGrafanaTarget&) = delete;
    ELogGrafanaTarget(ELogGrafanaTarget&&) = delete;
    ~ELogGrafanaTarget() override {}

    bool handleResult(const httplib::Result& result);

protected:
    /** @brief Order the log target to start (required for threaded targets). */
    bool startLogTarget() override;

    /** @brief Order the log target to stop (required for threaded targets). */
    bool stopLogTarget() override;

    std::string m_lokiAddress;
    ELogHttpClient m_client;
    uint32_t m_connectTimeoutMillis;
    uint32_t m_writeTimeoutMillis;
    uint32_t m_readTimeoutMillis;
    uint32_t m_resendPeriodMillis;
    uint32_t m_backlogLimitBytes;
    uint32_t m_shutdownTimeoutMillis;
};

}  // namespace elog

#endif  // ELOG_ENABLE_GRAFANA_CONNECTOR

#endif  // __ELOG_GRAFANA_TARGET_H__