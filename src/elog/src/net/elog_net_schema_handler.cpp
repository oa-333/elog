#include "net/elog_net_schema_handler.h"

#ifdef ELOG_ENABLE_NET

#include <cassert>

#include "elog_common.h"
#include "elog_config_loader.h"
#include "elog_report.h"
#include "elog_schema_handler_internal.h"
#include "net/elog_net_target_provider.h"

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogNetSchemaHandler)

ELOG_IMPLEMENT_SCHEMA_HANDLER(ELogNetSchemaHandler)

bool ELogNetSchemaHandler::registerPredefinedProviders() {
    // register predefined providers
    if (!initNamedTargetProvider<ELogNetTargetProvider>(ELOG_REPORT_LOGGER, this, "tcp")) {
        return false;
    }
    if (!initNamedTargetProvider<ELogNetTargetProvider>(ELOG_REPORT_LOGGER, this, "udp")) {
        return false;
    }
    return true;
}

}  // namespace elog

#endif  // ELOG_ENABLE_NET
