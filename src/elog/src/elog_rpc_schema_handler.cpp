#include "elog_rpc_schema_handler.h"

#include <cassert>

#include "elog_config_loader.h"
#include "elog_config_parser.h"
#include "elog_error.h"

#ifdef ELOG_ENABLE_GRPC_CONNECTOR
#include "elog_grpc_target_provider.h"
#endif

namespace elog {

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

ELogTarget* ELogRpcSchemaHandler::loadTarget(const std::string& logTargetCfg,
                                             const ELogTargetSpec& targetSpec) {
    // the path represents the RPC provider type name
    // current predefined types are supported:
    // grpc
    const std::string& rpcProvider = targetSpec.m_path;

    // in addition, we expect at least 'rpc_server' property and 'rpc_call'
    if (targetSpec.m_props.size() < 2) {
        ELOG_REPORT_ERROR(
            "Invalid RPC log target specification, expected at least one property: %s",
            logTargetCfg.c_str());
        return nullptr;
    }

    // get rpc_server property and parse it as host:port
    ELogPropertyMap::const_iterator itr = targetSpec.m_props.find("rpc_server");
    if (itr == targetSpec.m_props.end()) {
        ELOG_REPORT_ERROR(
            "Invalid message queue log target specification, missing property msgq_topic: %s",
            logTargetCfg.c_str());
        return nullptr;
    }
    const std::string& rpcServer = itr->second;
    std::string host;
    int port = 0;
    if (!ELogConfigParser::parseHostPort(rpcServer, host, port)) {
        ELOG_REPORT_ERROR("Failed to parse rpc_server property '%s' as host:port: %s",
                          rpcServer.c_str(), logTargetCfg.c_str());
        return nullptr;
    }

    // get rpc_call
    std::string rpcCall;
    itr = targetSpec.m_props.find("rpc_call");
    if (itr != targetSpec.m_props.end()) {
        rpcCall = itr->second;
    }

    // now parse it as func-name(params)
    std::string::size_type openParenPos = rpcCall.find('(');
    if (openParenPos == std::string::npos) {
        ELOG_REPORT_ERROR("Invalid rpc_call specification '%s', missing open parenthesis: %s",
                          rpcCall.c_str(), logTargetCfg.c_str());
        return nullptr;
    }
    if (rpcCall.back() != ')') {
        ELOG_REPORT_ERROR(
            "Invalid rpc_call specification '%s', missing closing parenthesis at end of call: %s",
            rpcCall.c_str(), logTargetCfg.c_str());
        return nullptr;
    }
    std::string functionName = rpcCall.substr(0, openParenPos);
    std::string params = rpcCall.substr(openParenPos + 1, rpcCall.length() - openParenPos - 2);

    ProviderMap::iterator providerItr = m_providerMap.find(rpcProvider);
    if (providerItr != m_providerMap.end()) {
        ELogRpcTargetProvider* provider = providerItr->second;
        return provider->loadTarget(logTargetCfg, targetSpec, rpcServer, host, port, functionName,
                                    params);
    }

    ELOG_REPORT_ERROR("Invalid RPC log target specification, unsupported RPC provider type %s: %s",
                      rpcProvider.c_str(), logTargetCfg.c_str());
    return nullptr;
}

ELogTarget* ELogRpcSchemaHandler::loadTarget(const std::string& logTargetCfg,
                                             const ELogTargetNestedSpec& targetNestedSpec) {
    // first make sure there are no log target sub-specs
    if (targetNestedSpec.m_subSpec.find("log_target") != targetNestedSpec.m_subSpec.end()) {
        ELOG_REPORT_ERROR("Message queue log target cannot have sub-targets: %s",
                          logTargetCfg.c_str());
        return nullptr;
    }

    // implementation is identical to URL style
    return loadTarget(logTargetCfg, targetNestedSpec.m_spec);
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

}  // namespace elog
