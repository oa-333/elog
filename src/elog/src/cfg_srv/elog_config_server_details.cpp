#include "cfg_srv/elog_config_server_details.h"

#ifdef ELOG_ENABLE_CONFIG_SERVICE

#include "elog_report.h"

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogConfigServerDetails)

bool ELogConfigServerDetails::setDetails(const char* server) {
    m_details = server;
    std::string::size_type colonPos = m_details.find(':');
    if (colonPos == std::string::npos) {
        ELOG_REPORT_ERROR("Invalid server specification, missing colon between host and port: %s",
                          server);
        return false;
    }

    std::string portStr = m_details.substr(colonPos + 1);
    std::size_t endPos = 0;
    try {
        m_port = std::stoi(portStr, &endPos, 10);
        m_host = m_details.substr(0, colonPos);
    } catch (std::exception& e) {
        ELOG_REPORT_ERROR("Invalid port: %s (error: %s)", portStr.c_str(), e.what());
        m_host.clear();
        m_port = 0;
        m_details.clear();
        return false;
    }

    return true;
}

}  // namespace elog

#endif  // ELOG_ENABLE_CONFIG_SERVICE