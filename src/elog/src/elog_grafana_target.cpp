#include "elog_grafana_target.h"

#ifdef ELOG_ENABLE_GRAFANA_CONNECTOR

#include "elog_error.h"

namespace elog {

bool ELogGrafanaTarget::startLogTarget() {
    // TODO: connect timeout, write timeout, security/authentication (SSL with various options)
    ELOG_REPORT_TRACE("Creating HTTP client to Grafana loki at: %s", m_lokiEndpoint.c_str());
    m_client = new httplib::Client(m_lokiEndpoint);
    ELOG_REPORT_TRACE("HTTP client created");
    if (!m_client->is_valid()) {
        ELOG_REPORT_ERROR("Connection to Grafana loki endpoint %s is not valid",
                          m_lokiEndpoint.c_str());
        delete m_client;
        m_client = nullptr;
        return false;
    }

    // set timeouts
    ELOG_REPORT_TRACE("Grafana Loki log target setting connect timeout millis to %u",
                      m_connectTimeoutMillis);
    m_client->set_connection_timeout(
        std::chrono::milliseconds(m_connectTimeoutMillis));  // micro-seconds
    m_client->set_write_timeout(std::chrono::milliseconds(m_writeTimeoutMillis));
    m_client->set_read_timeout(std::chrono::milliseconds(m_readTimeoutMillis));

    return true;
}

bool ELogGrafanaTarget::stopLogTarget() {
    if (m_client != nullptr) {
        delete m_client;
        m_client = nullptr;
    }
    return true;
}

}  // namespace elog

#endif  // ELOG_ENABLE_GRAFANA_CONNECTOR