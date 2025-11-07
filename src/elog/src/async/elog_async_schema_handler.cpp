#include "async/elog_async_schema_handler.h"

#include <cassert>

#include "async/elog_deferred_target_provider.h"
#include "async/elog_multi_quantum_target_provider.h"
#include "async/elog_quantum_target_provider.h"
#include "async/elog_queued_target_provider.h"
#include "elog_config_loader.h"
#include "elog_report.h"
#include "elog_schema_handler_internal.h"

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogAsyncSchemaHandler)

ELOG_IMPLEMENT_SCHEMA_HANDLER(ELogAsyncSchemaHandler)

bool ELogAsyncSchemaHandler::registerPredefinedProviders() {
    // register predefined providers
    if (!initTargetProvider<ELogDeferredTargetProvider>(ELOG_REPORT_LOGGER, this, "deferred")) {
        return false;
    }
    if (!initTargetProvider<ELogQueuedTargetProvider>(ELOG_REPORT_LOGGER, this, "queued")) {
        return false;
    }
    if (!initTargetProvider<ELogQuantumTargetProvider>(ELOG_REPORT_LOGGER, this, "quantum")) {
        return false;
    }
    if (!initTargetProvider<ELogMultiQuantumTargetProvider>(ELOG_REPORT_LOGGER, this,
                                                            "multi_quantum")) {
        return false;
    }
    return true;
}

}  // namespace elog
