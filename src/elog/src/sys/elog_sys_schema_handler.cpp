#include "sys/elog_sys_schema_handler.h"

#include "elog_config_loader.h"
#include "elog_report.h"
#include "file/elog_file_target.h"
#include "sys/elog_syslog_target.h"
#include "sys/elog_win32_event_log_target.h"

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogSysSchemaHandler)

ELogTarget* ELogSysSchemaHandler::loadTarget(const ELogConfigMapNode* logTargetCfg) {
    // load target type
    std::string providerType;
    if (!ELogConfigLoader::getLogTargetStringProperty(logTargetCfg, "system", "type",
                                                      providerType)) {
        return nullptr;
    }

    // load statistics enable flag if any
    bool enableStats = true;
    if (!ELogConfigLoader::getOptionalLogTargetBoolProperty(logTargetCfg, "system", "enable_stats",
                                                            enableStats)) {
        return nullptr;
    }

    ELogTarget* logTarget = nullptr;
    if (providerType.compare("stderr") == 0) {
        logTarget = new (std::nothrow) ELogFileTarget(stderr, nullptr, false, enableStats);
    } else if (providerType.compare("stdout") == 0) {
        logTarget = new (std::nothrow) ELogFileTarget(stdout, nullptr, false, enableStats);
    } else if (providerType.compare("syslog") == 0) {
#ifdef ELOG_LINUX
        logTarget = new (std::nothrow) ELogSysLogTarget(enableStats);
#else
        ELOG_REPORT_ERROR("Cannot create syslog log target, not supported on current platform");
        return nullptr;
#endif
    } else if (providerType.compare("eventlog") == 0) {
#ifdef ELOG_WINDOWS
        std::string eventSourceName;
        if (!ELogConfigLoader::getOptionalLogTargetStringProperty(
                logTargetCfg, "system", "event_source_name", eventSourceName)) {
            return nullptr;
        }
        uint32_t eventId = ELOG_DEFAULT_WIN32_EVENT_LOG_ID;
        if (!ELogConfigLoader::getOptionalLogTargetUInt32Property(logTargetCfg, "system",
                                                                  "event_id", eventId)) {
            return nullptr;
        }
        logTarget = new (std::nothrow) ELogWin32EventLogTarget(eventSourceName.c_str(), eventId);
#else
        ELOG_REPORT_ERROR("Cannot create eventlog log target, not supported on current platform");
        return nullptr;
#endif
    } else {
        ELOG_REPORT_ERROR("Unrecognized sys log target type: %s", providerType.c_str());
        return nullptr;
    }

    if (logTarget == nullptr) {
        ELOG_REPORT_ERROR("Failed to create system log target, out of memory");
        return nullptr;
    }
    return logTarget;
}

}  // namespace elog
