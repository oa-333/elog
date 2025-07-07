#include "elog_sys_schema_handler.h"

#include "elog_config_loader.h"
#include "elog_error.h"
#include "elog_file_target.h"
#include "elog_syslog_target.h"

namespace elog {

ELogTarget* ELogSysSchemaHandler::loadTarget(const ELogConfigMapNode* logTargetCfg) {
    std::string path;
    if (!ELogConfigLoader::getLogTargetStringProperty(logTargetCfg, "system", "type", path)) {
        return nullptr;
    }
    ELogTarget* logTarget = nullptr;
    if (path.compare("stderr") == 0) {
        logTarget = new (std::nothrow) ELogFileTarget(stderr);
    }
    if (path.compare("stdout") == 0) {
        logTarget = new (std::nothrow) ELogFileTarget(stdout);
    }
    if (path.compare("syslog") == 0) {
#ifdef ELOG_LINUX
        logTarget = new (std::nothrow) ELogSysLogTarget();
#else
        ELOG_REPORT_ERROR("Cannot create syslog log target, not supported on current platform");
        return nullptr;
#endif
    }

    if (logTarget == nullptr) {
        if (logTarget == nullptr) {
            ELOG_REPORT_ERROR("Failed to create system log target, out of memory");
            return nullptr;
        }
    }
    return logTarget;
}

}  // namespace elog
