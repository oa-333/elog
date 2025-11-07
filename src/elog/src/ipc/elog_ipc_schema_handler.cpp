#include "ipc/elog_ipc_schema_handler.h"

#ifdef ELOG_ENABLE_IPC

#include <cassert>

#include "elog_common.h"
#include "elog_config_loader.h"
#include "elog_report.h"
#include "elog_schema_handler_internal.h"
#include "ipc/elog_pipe_target_provider.h"

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogIpcSchemaHandler)

ELOG_IMPLEMENT_SCHEMA_HANDLER(ELogIpcSchemaHandler)

bool ELogIpcSchemaHandler::registerPredefinedProviders() {
    // register predefined providers
    if (!initNamedTargetProvider<ELogPipeTargetProvider>(ELOG_REPORT_LOGGER, this, "pipe")) {
        return false;
    }
    return true;
}

}  // namespace elog

#endif  // ELOG_ENABLE_NET
