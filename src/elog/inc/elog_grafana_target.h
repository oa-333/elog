#ifndef __ELOG_GRAFANA_TARGET_H__
#define __ELOG_GRAFANA_TARGET_H__

#include "elog_def.h"

#ifdef ELOG_ENABLE_GRAFANA_CONNECTOR

#include <httplib.h>

#include "elog_http_client.h"
#include "elog_mon_target.h"

namespace elog {

// both json and gRPC Grafana clients use HTTP client, so we factor out common code

class ELOG_API ELogGrafanaTarget : public ELogMonTarget, public ELogHttpClientAssistant {
public:
    ELogGrafanaTarget(const char* lokiAddress, const ELogHttpConfig& config);
    ELogGrafanaTarget(const ELogGrafanaTarget&) = delete;
    ELogGrafanaTarget(ELogGrafanaTarget&&) = delete;
    ~ELogGrafanaTarget() override {}

protected:
    /** @brief Order the log target to start (required for threaded targets). */
    bool startLogTarget() override;

    /** @brief Order the log target to stop (required for threaded targets). */
    bool stopLogTarget() override;

    ELogHttpClient m_client;
};

}  // namespace elog

#endif  // ELOG_ENABLE_GRAFANA_CONNECTOR

#endif  // __ELOG_GRAFANA_TARGET_H__