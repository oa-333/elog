#ifndef __ELOG_CONFIG_SERVER_DETAILS_H__
#define __ELOG_CONFIG_SERVER_DETAILS_H__

#ifdef ELOG_ENABLE_CONFIG_SERVICE

#include <string>
#include <unordered_map>
#include <vector>

#include "elog_common_def.h"
#include "elog_config_service_publisher.h"
#include "elog_def.h"

namespace elog {

/** @struct Details of a service discovery server (e.g. redis, etcd, Consul, etc.). */
class ELOG_API ELogConfigServerDetails {
public:
    /** @brief COnstructs details from explicit host and port. */
    ELogConfigServerDetails(const char* host = "", int port = 0) : m_host(host), m_port(port) {
        m_details = m_host + ":" + std::to_string(port);
    }
    ELogConfigServerDetails(const ELogConfigServerDetails&) = default;
    ELogConfigServerDetails(ELogConfigServerDetails&&) = default;
    ELogConfigServerDetails& operator=(const ELogConfigServerDetails&) = default;
    ~ELogConfigServerDetails() {}

    /**
     * @brief Sets the server details. expected foramt is host:port. The host and port members are
     * set as well after successful parsing.
     * @return The operation's result.
     */
    bool setDetails(const char* server);

    /** @brief Queries whether the object is valid (i.e. all details are properly set). */
    inline bool isValid() const { return m_port != 0 && !m_host.empty() && !m_details.empty(); }

    /** @brief Retrieves the host name (or IP address). */
    inline const char* getHost() const { return m_host.c_str(); }

    /** @brief Retrieves the port number. */
    inline int getPort() const { return m_port; }

    /** @brief Retrieves the details string in the form host:port. */
    inline const char* getDetails() const { return m_details.c_str(); }

private:
    std::string m_host;
    int m_port;
    std::string m_details;
};

/** @typedef List of server details. */
typedef std::vector<ELogConfigServerDetails> ELogConfigServerList;

}  // namespace elog

#endif  // ELOG_ENABLE_CONFIG_SERVICE

#endif  // __ELOG_CONFIG_SERVER_DETAILS_H__