#include "rpc/elog_rpc_schema_handler.h"

#include <cassert>

#include "elog_config_loader.h"
#include "elog_config_parser.h"
#include "elog_report.h"

#ifdef ELOG_ENABLE_GRPC_CONNECTOR
#include "rpc/elog_grpc_target_provider.h"
#endif

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogRpcSchemaHandler)

template <typename T>
static bool initRpcTargetProvider(ELogRpcSchemaHandler* schemaHandler, const char* name) {
    T* provider = new (std::nothrow) T();
    if (provider == nullptr) {
        ELOG_REPORT_ERROR("Failed to create %s RPC target provider, out of memory", name);
        return false;
    }
    if (!schemaHandler->registerRpcTargetProvider(name, provider)) {
        ELOG_REPORT_ERROR("Failed to register %s RPC target provider, duplicate name", name);
        delete provider;
        return false;
    }
    return true;
}

ELogRpcSchemaHandler::~ELogRpcSchemaHandler() {
    for (auto& entry : m_providerMap) {
        delete entry.second;
    }
    m_providerMap.clear();
}

bool ELogRpcSchemaHandler::registerPredefinedProviders() {
    // register predefined providers
#ifdef ELOG_ENABLE_GRPC_CONNECTOR
    if (!initRpcTargetProvider<ELogGRPCTargetProvider>(this, "grpc")) {
        return false;
    }
#endif
    return true;
}

bool ELogRpcSchemaHandler::registerRpcTargetProvider(const char* providerName,
                                                     ELogRpcTargetProvider* provider) {
    return m_providerMap.insert(ProviderMap::value_type(providerName, provider)).second;
}

ELogTarget* ELogRpcSchemaHandler::loadTarget(const ELogConfigMapNode* logTargetCfg) {
    // the path represents the RPC provider type name
    // current predefined types are supported:
    // grpc
    std::string rpcProvider;
    if (!ELogConfigLoader::getLogTargetStringProperty(logTargetCfg, "RPC", "type", rpcProvider)) {
        return nullptr;
    }

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

    ProviderMap::iterator providerItr = m_providerMap.find(rpcProvider);
    if (providerItr != m_providerMap.end()) {
        ELogRpcTargetProvider* provider = providerItr->second;
        return provider->loadTarget(logTargetCfg, rpcServer, host, port, functionName, params);
    }

    ELOG_REPORT_ERROR(
        "Invalid RPC log target specification, unsupported RPC provider type %s (context: %s)",
        rpcProvider.c_str(), logTargetCfg->getFullContext());
    return nullptr;
}

void ELogRpcSchemaHandler::destroy() { delete this; }

}  // namespace elog
