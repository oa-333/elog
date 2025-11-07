#include "mon/elog_mon_schema_handler.h"

#include <cassert>

#include "elog_common.h"
#include "elog_config_loader.h"
#include "elog_report.h"
#include "elog_schema_handler_internal.h"

#ifdef ELOG_ENABLE_GRAFANA_CONNECTOR
#include "mon/elog_grafana_target_provider.h"
#endif

#ifdef ELOG_ENABLE_SENTRY_CONNECTOR
#include "mon/elog_sentry_target_provider.h"
#endif

#ifdef ELOG_ENABLE_DATADOG_CONNECTOR
#include "mon/elog_datadog_target_provider.h"
#endif

#ifdef ELOG_ENABLE_OTEL_CONNECTOR
#include "mon/elog_otel_target_provider.h"
#endif

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogMonSchemaHandler)

ELOG_IMPLEMENT_SCHEMA_HANDLER(ELogMonSchemaHandler)

bool ELogMonSchemaHandler::registerPredefinedProviders() {
    // register predefined providers
#ifdef ELOG_ENABLE_GRAFANA_CONNECTOR
    if (!initTargetProvider<ELogGrafanaTargetProvider>(ELOG_REPORT_LOGGER, this, "grafana")) {
        return false;
    }
#endif
#ifdef ELOG_ENABLE_SENTRY_CONNECTOR
    if (!initTargetProvider<ELogSentryTargetProvider>(ELOG_REPORT_LOGGER, this, "sentry")) {
        return false;
    }
#endif
#ifdef ELOG_ENABLE_DATADOG_CONNECTOR
    if (!initTargetProvider<ELogDatadogTargetProvider>(ELOG_REPORT_LOGGER, this, "datadog")) {
        return false;
    }
#endif
#ifdef ELOG_ENABLE_OTEL_CONNECTOR
    if (!initTargetProvider<ELogOtelTargetProvider>(ELOG_REPORT_LOGGER, this, "otel")) {
        return false;
    }
#endif
    return true;
}

}  // namespace elog
