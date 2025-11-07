#ifndef __ELOG_RPC_SCHEMA_HANDLER_H__
#define __ELOG_RPC_SCHEMA_HANDLER_H__

#include <unordered_map>

#include "elog_rpc_target_provider.h"
#include "elog_schema_handler.h"

namespace elog {

/** @brief Handler for loading RPC log target from configuration. */
class ELOG_API ELogRpcSchemaHandler : public ELogSchemaHandler {
public:
    ELogRpcSchemaHandler() : ELogSchemaHandler(SCHEME_NAME) {}
    ELogRpcSchemaHandler(const ELogRpcSchemaHandler&) = delete;
    ELogRpcSchemaHandler(ELogRpcSchemaHandler&&) = delete;
    ELogRpcSchemaHandler& operator=(const ELogRpcSchemaHandler&) = delete;

    static constexpr const char* SCHEME_NAME = "rpc";

    /** @brief Registers predefined target providers. */
    bool registerPredefinedProviders() final;

    ELOG_DECLARE_SCHEMA_HANDLER(ELogRpcSchemaHandler)
};

}  // namespace elog

#endif  // __ELOG_RPC_SCHEMA_HANDLER_H__