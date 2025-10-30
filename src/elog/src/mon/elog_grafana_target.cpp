#include "mon/elog_grafana_target.h"

#ifdef ELOG_ENABLE_GRAFANA_CONNECTOR

#include "elog_json_receptor.h"
#include "elog_report.h"

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogGrafanaTarget)

ELogGrafanaTarget::ELogGrafanaTarget(const char* lokiAddress, const ELogHttpConfig& config)
    : ELogHttpClientAssistant("Grafana Loki", ELOG_HTTP_STATUS_NO_CONTENT) {
    ELOG_REPORT_TRACE("Creating HTTP client to Grafana loki at: %s", lokiAddress);
    m_client.initialize(lokiAddress, "Grafana Loki", config, this);
}

bool ELogGrafanaTarget::startLogTarget() { return m_client.start(); }

bool ELogGrafanaTarget::stopLogTarget() { return m_client.stop(); }

}  // namespace elog

#endif  // ELOG_ENABLE_GRAFANA_CONNECTOR