#include "elog_grpc_target_provider.h"

#ifdef ELOG_ENABLE_GRPC_CONNECTOR

#include <fstream>
#include <sstream>

#include "elog_common.h"
#include "elog_error.h"
#include "elog_grpc_target.h"

#define ELOG_MAX_GRPC_TARGETS 10

namespace elog {

// register default implementation
DECLARE_ELOG_GRPC_TARGET(elog_grpc::ELogGRPCService, elog_grpc::ELogGRPCRecordMsg,
                         elog_grpc::ELogGRPCStatus, elog)

struct ELogGRPCTargetNameConstructor {
    const char* m_name;
    ELogGRPCBaseTargetConstructor* m_ctor;
};

static ELogGRPCTargetNameConstructor sTargetConstructors[ELOG_MAX_GRPC_TARGETS] = {};
static uint32_t sTargetConstructorsCount = 0;

typedef std::unordered_map<std::string, ELogGRPCBaseTargetConstructor*>
    ELogGRPCTargetConstructorMap;

static ELogGRPCTargetConstructorMap sTargetConstructorMap;

static bool readFile(const std::string& path, std::string& contents) {
    std::ifstream f(path);
    if (!f) {
        ELOG_REPORT_ERROR("Failed to open file for reading: %s", path.c_str());
        return false;
    }
    std::stringstream s;
    s << f.rdbuf();
    contents = s.str();
    return true;
}

void registerGRPCTargetConstructor(const char* name,
                                   ELogGRPCBaseTargetConstructor* targetConstructor) {
    // due to c runtime issues we delay access to unordered map
    if (sTargetConstructorsCount >= ELOG_MAX_GRPC_TARGETS) {
        ELOG_REPORT_ERROR("Cannot register GRPC target constructor, no space: %s", name);
        exit(1);
    } else {
        sTargetConstructors[sTargetConstructorsCount++] = {name, targetConstructor};
    }
}

static bool applyGRPCTargetConstructorRegistration() {
    for (uint32_t i = 0; i < sTargetConstructorsCount; ++i) {
        ELogGRPCTargetNameConstructor& nameCtorPair = sTargetConstructors[i];
        if (!sTargetConstructorMap
                 .insert(ELogGRPCTargetConstructorMap::value_type(nameCtorPair.m_name,
                                                                  nameCtorPair.m_ctor))
                 .second) {
            ELOG_REPORT_ERROR("Duplicate GRPC target constructor identifier: %s",
                              nameCtorPair.m_name);
            return false;
        }
    }
    return true;
}

static bool initGRPCTargetConstructors() { return applyGRPCTargetConstructorRegistration(); }

static void termGRPCTargetConstructors() { sTargetConstructorMap.clear(); }

static ELogRpcTarget* constructGRPCTarget(const char* name, const std::string& server,
                                          const std::string& params, const std::string& serverCA,
                                          const std::string& clientCA, const std::string& clientKey,
                                          ELogGRPCClientMode clientMode,
                                          uint32_t deadlineTimeoutMillis,
                                          uint32_t maxInflightCalls) {
    ELogGRPCTargetConstructorMap::iterator itr = sTargetConstructorMap.find(name);
    if (itr == sTargetConstructorMap.end()) {
        ELOG_REPORT_ERROR("Invalid gPRC target provider type name '%s': not found", name);
        return nullptr;
    }

    ELogGRPCBaseTargetConstructor* constructor = itr->second;
    ELogRpcTarget* logTarget = constructor->createLogTarget(
        ELogError::getErrorHandler(), server, params, serverCA, clientCA, clientKey, clientMode,
        deadlineTimeoutMillis, maxInflightCalls);
    if (logTarget == nullptr) {
        ELOG_REPORT_ERROR("Failed to create gRPC target by name '%s', out of memory", name);
    }
    return logTarget;
}

ELogGRPCTargetProvider::ELogGRPCTargetProvider() { initGRPCTargetConstructors(); }

ELogGRPCTargetProvider::~ELogGRPCTargetProvider() { termGRPCTargetConstructors(); }

ELogRpcTarget* ELogGRPCTargetProvider::loadTarget(
    const std::string& logTargetCfg, const ELogTargetSpec& targetSpec, const std::string& server,
    const std::string& host, int port, const std::string& functionName, const std::string& params) {
    // a provider type may be specified (if none, then default implementation is used)
    std::string providerType = "elog";
    ELogPropertyMap::const_iterator itr = targetSpec.m_props.find("grpc_provider_type");
    if (itr != targetSpec.m_props.end()) {
        providerType = itr->second;
    }

    // a deadline may also be specified
    uint32_t deadlineTimeoutMillis = -1;
    itr = targetSpec.m_props.find("grpc_deadline_timeout_millis");
    if (itr != targetSpec.m_props.end()) {
        if (!parseIntProp("grpc_deadline_timeout_millis", logTargetCfg, itr->second,
                          deadlineTimeoutMillis, true)) {
            ELOG_REPORT_ERROR(
                "Invalid log target specification, invalid gRPC deadline timeout millis value: %s",
                logTargetCfg.c_str());
            return nullptr;
        }
    }

    // check for client type: simple, stream
    ELogGRPCClientMode clientMode = ELogGRPCClientMode::GRPC_CM_UNARY;
    itr = targetSpec.m_props.find("grpc_client_mode");
    if (itr != targetSpec.m_props.end()) {
        const std::string& clientModeStr = itr->second;
        if (clientModeStr.compare("unary") == 0) {
            clientMode = ELogGRPCClientMode::GRPC_CM_UNARY;
        } else if (clientModeStr.compare("stream") == 0) {
            clientMode = ELogGRPCClientMode::GRPC_CM_STREAM;
        } else if (clientModeStr.compare("async") == 0) {
            clientMode = ELogGRPCClientMode::GRPC_CM_ASYNC;
        } else if (clientModeStr.compare("async_callback_unary") == 0) {
            clientMode = ELogGRPCClientMode::GRPC_CM_ASYNC_CALLBACK_UNARY;
        } else if (clientModeStr.compare("async_callback_stream") == 0) {
            clientMode = ELogGRPCClientMode::GRPC_CM_ASYNC_CALLBACK_STREAM;
        } else {
            ELOG_REPORT_ERROR(
                "Invalid log target specification, invalid gRPC client mode value '%s': %s",
                clientModeStr.c_str(), logTargetCfg.c_str());
            return nullptr;
        }
    }

    // for async callback stream, it is also possible to specify grpc_max_inflight_calls
    uint32_t maxInflightCalls = ELOG_GRPC_DEFAULT_MAX_INFLIGHT_CALLS;
    itr = targetSpec.m_props.find("grpc_max_inflight_calls");
    if (itr != targetSpec.m_props.end()) {
        if (!parseIntProp("grpc_max_inflight_calls", logTargetCfg, itr->second, maxInflightCalls,
                          true)) {
            ELOG_REPORT_ERROR(
                "Invalid log target specification, invalid gRPC max inflight calls value: %s",
                logTargetCfg.c_str());
            return nullptr;
        }
    }

    // check for security members
    std::string serverCA;
    itr = targetSpec.m_props.find("grpc_server_ca_path");
    if (itr != targetSpec.m_props.end()) {
        const std::string& serverCAPath = itr->second;
        if (!readFile(serverCAPath, serverCA)) {
            ELOG_REPORT_ERROR(
                "Invalid log target specification, could not read gRPC server certificate "
                "authority file from path '%s': %s",
                serverCAPath.c_str(), logTargetCfg.c_str());
            return nullptr;
        }
    }

    std::string clientCA;
    itr = targetSpec.m_props.find("grpc_client_ca_path");
    if (itr != targetSpec.m_props.end()) {
        const std::string& clientCAPath = itr->second;
        if (!readFile(clientCAPath, clientCA)) {
            ELOG_REPORT_ERROR(
                "Invalid log target specification, could not read gRPC client certificate "
                "authority file from path '%s': %s",
                clientCAPath.c_str(), logTargetCfg.c_str());
            return nullptr;
        }
    }

    std::string clientKey;
    itr = targetSpec.m_props.find("grpc_client_key_path");
    if (itr != targetSpec.m_props.end()) {
        const std::string& clientKeyPath = itr->second;
        if (!readFile(clientKeyPath, clientKey)) {
            ELOG_REPORT_ERROR(
                "Invalid log target specification, could not read gRPC client key file from path "
                "'%s': %s",
                clientKeyPath.c_str(), logTargetCfg.c_str());
            return nullptr;
        }
    }

    // search for the provider type and construct the specialized log target
    return constructGRPCTarget(providerType.c_str(), server, params, serverCA, clientCA, clientKey,
                               clientMode, deadlineTimeoutMillis, maxInflightCalls);
}

}  // namespace elog

#endif  // ELOG_ENABLE_GRPC_CONNECTOR
