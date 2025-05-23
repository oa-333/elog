#include "elog_grpc_target_provider.h"

#ifdef ELOG_ENABLE_GRPC_CONNECTOR

#include "elog_common.h"
#include "elog_error.h"
#include "elog_grpc_target.h"

namespace elog {

ELogRpcTarget* ELogGRPCTargetProvider::loadTarget(
    const std::string& logTargetCfg, const ELogTargetSpec& targetSpec, const std::string& server,
    const std::string& host, int port, const std::string& functionName, const std::string& params) {
    // a deadline may also be specified
    uint32_t deadlineTimeoutMillis = -1;
    ELogPropertyMap::const_iterator itr = targetSpec.m_props.find("grpc_deadline_timeout_millis");
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
    uint32_t maxInflightCalls = 0;
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

    ELogGRPCTarget* target = new (std::nothrow)
        ELogGRPCTarget(server, params, clientMode, deadlineTimeoutMillis, maxInflightCalls);
    if (target == nullptr) {
        ELOG_REPORT_ERROR("Failed to allocate gRPC message queue log target, out of memory");
    }
    return target;
}

}  // namespace elog

#endif  // ELOG_ENABLE_GRPC_CONNECTOR
