#include "msgq/elog_msgq_schema_handler.h"

#include <cassert>

#include "elog_config_loader.h"
#include "elog_report.h"
#include "elog_schema_handler_internal.h"
#include "msgq/elog_kafka_msgq_target_provider.h"

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogMsgQSchemaHandler)

ELOG_IMPLEMENT_SCHEMA_HANDLER(ELogMsgQSchemaHandler)

bool ELogMsgQSchemaHandler::registerPredefinedProviders() {
    // register predefined providers
#ifdef ELOG_ENABLE_KAFKA_MSGQ_CONNECTOR
    if (!initTargetProvider<ELogKafkaMsgQTargetProvider>(ELOG_REPORT_LOGGER, this, "kafka")) {
        return false;
    }
#endif
    return true;
}

}  // namespace elog
