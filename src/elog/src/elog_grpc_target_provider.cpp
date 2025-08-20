#include "elog_grpc_target_provider.h"

#ifdef ELOG_ENABLE_GRPC_CONNECTOR

#include <fstream>
#include <sstream>

#include "elog_common.h"
#include "elog_config_loader.h"
#include "elog_grpc_target.h"
#include "elog_report.h"

#define ELOG_MAX_GRPC_TARGETS 10

namespace elog {

// register default implementation
DECLARE_ELOG_GRPC_TARGET(elog_grpc::ELogService, elog_grpc::ELogRecordMsg, elog_grpc::ELogStatusMsg,
                         elog)

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
                                          uint64_t deadlineTimeoutMillis,
                                          uint32_t maxInflightCalls) {
    ELogGRPCTargetConstructorMap::iterator itr = sTargetConstructorMap.find(name);
    if (itr == sTargetConstructorMap.end()) {
        ELOG_REPORT_ERROR("Invalid gPRC target provider type name '%s': not found", name);
        return nullptr;
    }

    ELogGRPCBaseTargetConstructor* constructor = itr->second;
    ELogRpcTarget* logTarget = constructor->createLogTarget(
        ELogReport::getReportHandler(), server, params, serverCA, clientCA, clientKey, clientMode,
        deadlineTimeoutMillis, maxInflightCalls);
    if (logTarget == nullptr) {
        ELOG_REPORT_ERROR("Failed to create gRPC target by name '%s', out of memory", name);
    }
    return logTarget;
}

ELogGRPCTargetProvider::ELogGRPCTargetProvider() { initGRPCTargetConstructors(); }

ELogGRPCTargetProvider::~ELogGRPCTargetProvider() { termGRPCTargetConstructors(); }

ELogRpcTarget* ELogGRPCTargetProvider::loadTarget(const ELogConfigMapNode* logTargetCfg,
                                                  const std::string& server,
                                                  const std::string& host, int port,
                                                  const std::string& functionName,
                                                  const std::string& params) {
    // a provider type may be specified (if none, then default implementation is used)
    std::string providerType = "elog";
    if (!ELogConfigLoader::getOptionalLogTargetStringProperty(logTargetCfg, "gRPC",
                                                              "grpc_provider_type", providerType)) {
        return nullptr;
    }

    // a deadline may also be specified
    uint64_t deadlineTimeoutMillis = ELOG_GRPC_DEFAULT_DEADLINE_MILLIS;
    if (!ELogConfigLoader::getOptionalLogTargetTimeoutProperty(
            logTargetCfg, "gRPC", "grpc_deadline_timeout", deadlineTimeoutMillis,
            ELogTimeUnits::TU_MILLI_SECONDS)) {
        return nullptr;
    }

    // check for client type: simple, stream
    std::string clientModeStr;
    bool found = false;
    if (!ELogConfigLoader::getOptionalLogTargetStringProperty(
            logTargetCfg, "gRPC", "grpc_client_mode", clientModeStr, &found)) {
        return nullptr;
    }
    ELogGRPCClientMode clientMode = ELogGRPCClientMode::GRPC_CM_UNARY;
    if (found) {
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
                "Invalid log target specification, invalid gRPC client mode value '%s' (context: "
                "%s)",
                clientModeStr.c_str(), logTargetCfg->getFullContext());
            return nullptr;
        }
    }

    // for async callback stream, it is also possible to specify grpc_max_inflight_calls
    uint32_t maxInflightCalls = ELOG_GRPC_DEFAULT_MAX_INFLIGHT_CALLS;
    if (!ELogConfigLoader::getOptionalLogTargetUInt32Property(
            logTargetCfg, "gRPC", "grpc_max_inflight_calls", maxInflightCalls)) {
        return nullptr;
    }

    // check for security members
    std::string serverCA;
    if (!loadFileProp(logTargetCfg, "grpc_server_ca_path", "server certificate authority",
                      serverCA)) {
        return nullptr;
    }

    std::string clientCA;
    if (!loadFileProp(logTargetCfg, "grpc_client_ca_path", "client certificate authority",
                      clientCA)) {
        return nullptr;
    }

    std::string clientKey;
    if (!loadFileProp(logTargetCfg, "grpc_client_key_path", "client key", clientKey)) {
        return nullptr;
    }

    // search for the provider type and construct the specialized log target
    return constructGRPCTarget(providerType.c_str(), server, params, serverCA, clientCA, clientKey,
                               clientMode, deadlineTimeoutMillis, maxInflightCalls);
}

bool ELogGRPCTargetProvider::loadFileProp(const ELogConfigMapNode* logTargetCfg,
                                          const char* propName, const char* description,
                                          std::string& fileContents) {
    std::string path;
    bool found = false;
    if (!ELogConfigLoader::getOptionalLogTargetStringProperty(logTargetCfg, "gRPC", propName, path,
                                                              &found)) {
        return false;
    }
    if (found) {
        if (!readFile(path, fileContents)) {
            ELOG_REPORT_ERROR(
                "Invalid log target specification, could not read gRPC %s file from path '%s' "
                "(context: %s)",
                description, path.c_str(), logTargetCfg->getFullContext());
            return false;
        }
    }
    return true;
}

}  // namespace elog

#endif  // ELOG_ENABLE_GRPC_CONNECTOR
