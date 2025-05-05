#include "elog_sys_schema_handler.h"

#include "elog_file_target.h"
#include "elog_syslog_target.h"
#include "elog_system.h"

namespace elog {

ELogTarget* ELogSysSchemaHandler::loadTarget(const std::string& logTargetCfg,
                                             const ELogTargetSpec& logTargetSpec) {
    ELogTarget* logTarget = nullptr;
    if (logTargetSpec.m_path.compare("stderr") == 0) {
        logTarget = new (std::nothrow) ELogFileTarget(stderr);
    }
    if (logTargetSpec.m_path.compare("stdout") == 0) {
        logTarget = new (std::nothrow) ELogFileTarget(stdout);
    }
    if (logTargetSpec.m_path.compare("syslog") == 0) {
#ifdef ELOG_LINUX
        logTarget = new (std::nothrow) ELogSysLogTarget();
#else
        ELogSystem::reportError(
            "Cannot create syslog log target, not supported on current platform");
        return nullptr;
#endif
    }

    if (logTarget == nullptr) {
        if (logTarget == nullptr) {
            ELogSystem::reportError("Failed to create log target, out of memory");
            return nullptr;
        }
    }
    return logTarget;
}

}  // namespace elog
