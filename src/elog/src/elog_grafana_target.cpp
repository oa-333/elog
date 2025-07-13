#include "elog_grafana_target.h"

#ifdef ELOG_ENABLE_GRAFANA_CONNECTOR

#include "elog_error.h"
#include "elog_json_receptor.h"

namespace elog {

bool ELogGrafanaTarget::handleResult(const httplib::Result& result) {
    if (result->status != 204) {  // 204 is: request processed, no server content
        ELOG_REPORT_ERROR("Received error status %d from Grafana server", result->status);
        return false;
    }
    return true;
}

bool ELogGrafanaTarget::startLogTarget() {
    ELOG_REPORT_TRACE("Creating HTTP client to Grafana loki at: %s", m_lokiAddress.c_str());
    m_client.initialize(m_lokiAddress.c_str(), this, m_connectTimeoutMillis, m_writeTimeoutMillis,
                        m_readTimeoutMillis, m_resendPeriodMillis, m_backlogLimitBytes);
    return m_client.start();
}

bool ELogGrafanaTarget::stopLogTarget() { return m_client.stop(); }

}  // namespace elog

#endif  // ELOG_ENABLE_GRAFANA_CONNECTOR