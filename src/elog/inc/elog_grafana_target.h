#ifndef __ELOG_GRAFANA_TARGET_H__
#define __ELOG_GRAFANA_TARGET_H__

#include "elog_def.h"

#ifdef ELOG_ENABLE_GRAFANA_CONNECTOR

#include <httplib.h>

#include "elog_mon_target.h"

#define ELOG_GRAFANA_DEFAULT_CONNECT_TIMEOUT_MILLIS 5000
#define ELOG_GRAFANA_DEFAULT_WRITE_TIMEOUT_MILLIS 1000
#define ELOG_GRAFANA_DEFAULT_READ_TIMEOUT_MILLIS 1000

namespace elog {

// both json and gRPC Grafana clients use HTTP client, so we factor out common code

class ELOG_API ELogGrafanaTarget : public ELogMonTarget {
public:
    ELogGrafanaTarget(const std::string& lokiEndpoint,
                      uint32_t connectTimeoutMillis = ELOG_GRAFANA_DEFAULT_CONNECT_TIMEOUT_MILLIS,
                      uint32_t writeTimeoutMillis = ELOG_GRAFANA_DEFAULT_WRITE_TIMEOUT_MILLIS,
                      uint32_t readTimeoutMillis = ELOG_GRAFANA_DEFAULT_READ_TIMEOUT_MILLIS)
        : m_lokiEndpoint(lokiEndpoint),
          m_connectTimeoutMillis(connectTimeoutMillis),
          m_writeTimeoutMillis(writeTimeoutMillis),
          m_readTimeoutMillis(readTimeoutMillis),
          m_client(nullptr) {}

    ELogGrafanaTarget(const ELogGrafanaTarget&) = delete;
    ELogGrafanaTarget(ELogGrafanaTarget&&) = delete;
    ~ELogGrafanaTarget() override {}

protected:
    /** @brief Order the log target to start (required for threaded targets). */
    bool startLogTarget() override;

    /** @brief Order the log target to stop (required for threaded targets). */
    bool stopLogTarget() override;

    std::string m_lokiEndpoint;
    httplib::Client* m_client;
    uint32_t m_connectTimeoutMillis;
    uint32_t m_writeTimeoutMillis;
    uint32_t m_readTimeoutMillis;
};

}  // namespace elog

#endif  // ELOG_ENABLE_GRAFANA_CONNECTOR

#endif  // __ELOG_GRAFANA_TARGET_H__