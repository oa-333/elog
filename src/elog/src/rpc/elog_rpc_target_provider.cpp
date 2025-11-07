#include "rpc/elog_rpc_target_provider.h"

#include <cassert>

#include "elog_config_loader.h"
#include "elog_config_parser.h"
#include "elog_report.h"

#ifdef ELOG_ENABLE_GRPC_CONNECTOR
#include "rpc/elog_grpc_target_provider.h"
#endif

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogRpcTargetProvider)

ELogTarget* ELogRpcTargetProvider::loadTarget(const ELogConfigMapNode* logTargetCfg) {
    // get rpc_server property and parse it as host:port
    std::string rpcServer;
    if (!ELogConfigLoader::getLogTargetStringProperty(logTargetCfg, "RPC", "rpc_server",
                                                      rpcServer)) {
        return nullptr;
    }

    std::string host;
    int port = 0;
    if (!ELogConfigParser::parseHostPort(rpcServer, host, port)) {
        ELOG_REPORT_ERROR("Failed to parse rpc_server property '%s' as host:port (context: %s)",
                          rpcServer.c_str(), logTargetCfg->getFullContext());
        return nullptr;
    }

    // get rpc_call
    std::string rpcCall;
    if (!ELogConfigLoader::getLogTargetStringProperty(logTargetCfg, "RPC", "rpc_call", rpcCall)) {
        return nullptr;
    }

    // now parse it as func-name(params)
    std::string::size_type openParenPos = rpcCall.find('(');
    if (openParenPos == std::string::npos) {
        ELOG_REPORT_ERROR(
            "Invalid rpc_call specification '%s', missing open parenthesis (context: %s)",
            rpcCall.c_str(), logTargetCfg->getFullContext());
        return nullptr;
    }
    if (rpcCall.back() != ')') {
        ELOG_REPORT_ERROR(
            "Invalid rpc_call specification '%s', missing closing parenthesis at end of call "
            "(context: %s)",
            rpcCall.c_str(), logTargetCfg->getFullContext());
        return nullptr;
    }
    std::string functionName = rpcCall.substr(0, openParenPos);
    std::string params = rpcCall.substr(openParenPos + 1, rpcCall.length() - openParenPos - 2);

    return loadRpcTarget(logTargetCfg, rpcServer, host, port, functionName, params);
}

}  // namespace elog
