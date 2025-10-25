#include "cfg_srv/elog_config_service_user.h"

#ifdef ELOG_ENABLE_CONFIG_SERVICE

#include "elog_common.h"
#include "elog_config_parser.h"
#include "elog_report.h"

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogConfigServiceUser)

bool ELogConfigServiceUser::parseServerListString(const std::string& serverListStr) {
    std::vector<std::string> serverList;
    tokenize(serverListStr.c_str(), serverList, ";,");
    for (const std::string& server : serverList) {
        std::string host;
        int port = 0;
        if (!ELogConfigParser::parseHostPort(server, host, port)) {
            ELOG_REPORT_ERROR("Invalid server specification, cannot parse host and port: %s",
                              server.c_str());
            return false;
        }
        modifyServerList().push_back(ELogConfigServerDetails(host.c_str(), port));
    }
    return true;
}

}  // namespace elog

#endif  // ELOG_ENABLE_CONFIG_SERVICE