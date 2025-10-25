#ifndef __ELOG_CONFIG_SERVICE_USER_H__
#define __ELOG_CONFIG_SERVICE_USER_H__

#ifdef ELOG_ENABLE_CONFIG_SERVICE

#include <string>

#include "cfg_srv/elog_config_server_details.h"
#include "elog_common_def.h"

namespace elog {

/** @brief Helper class for managing a list of service discovery servers. */
class ELOG_API ELogConfigServiceUser {
public:
    ELogConfigServiceUser() {}
    ELogConfigServiceUser(ELogConfigServiceUser&) = delete;
    ELogConfigServiceUser(ELogConfigServiceUser&&) = delete;
    ELogConfigServiceUser& operator=(const ELogConfigServiceUser&) = delete;
    virtual ~ELogConfigServiceUser() {}

    /**
     * @brief Adds a service discovery server to the server list.
     * @param host The server's host name.
     * @param port The server's port.
     */
    inline void addServer(const char* host, int port) {
        modifyServerList().push_back(ELogConfigServerDetails(host, port));
    }

    /**
     * @brief Adds a service discovery server to the server list.
     * @param server The server's details (expected host:port).
     * @return The operation's result.
     */
    inline bool addServer(const char* server) {
        ELogConfigServerDetails serverDetails;
        if (!serverDetails.setDetails(server)) {
            return false;
        }
        modifyServerList().push_back(serverDetails);
        return true;
    }

    /**
     * @brief Sets the list of redis servers (comma or semicolon-separated list of strings, each
     * string in the form <host>:<port>).
     * @return The operation's result.
     */
    inline bool setServerList(const std::string& serverList) {
        return parseServerListString(serverList);
    }

    /** @brief Sets the list of servers from pairs of host and port. */
    inline void setServerList(const std::vector<std::pair<std::string, int>>& serverList) {
        ELogConfigServerList& localServerList = modifyServerList();
        localServerList.clear();
        for (const auto& server : serverList) {
            localServerList.push_back(ELogConfigServerDetails(server.first.c_str(), server.second));
        }
    }

    /** @brief Retrieves the list of servers in pairs of host and port. */
    inline const std::vector<std::pair<std::string, int>>& getServerList(
        std::vector<std::pair<std::string, int>>& serverList) {
        for (const auto& server : getServerList()) {
            serverList.push_back({server.getHost(), server.getPort()});
        }
        return serverList;
    }

protected:
    // since the server list may reside in a parameters struct, we use virtual acessors

    // access server list
    virtual const ELogConfigServerList& getServerList() const = 0;
    virtual ELogConfigServerList& modifyServerList() = 0;

    // helper class for parsing a server list given as a comma or semicolon-separated string
    bool parseServerListString(const std::string& serverListStr);
};

}  // namespace elog

#endif  // ELOG_ENABLE_CONFIG_SERVICE

#endif  // __ELOG_CONFIG_SERVICE_REDIS_USER_H__