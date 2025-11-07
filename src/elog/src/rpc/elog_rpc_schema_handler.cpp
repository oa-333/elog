#include "rpc/elog_rpc_schema_handler.h"

#include <cassert>

#include "elog_config_loader.h"
#include "elog_config_parser.h"
#include "elog_report.h"
#include "elog_schema_handler_internal.h"

#ifdef ELOG_ENABLE_GRPC_CONNECTOR
#include "rpc/elog_grpc_target_provider.h"
#endif

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogRpcSchemaHandler)

ELOG_IMPLEMENT_SCHEMA_HANDLER(ELogRpcSchemaHandler)

bool ELogRpcSchemaHandler::registerPredefinedProviders() {
    // register predefined providers
#ifdef ELOG_ENABLE_GRPC_CONNECTOR
    if (!initTargetProvider<ELogGRPCTargetProvider>(ELOG_REPORT_LOGGER, this, "grpc")) {
        return false;
    }
#endif
    return true;
}

}  // namespace elog
