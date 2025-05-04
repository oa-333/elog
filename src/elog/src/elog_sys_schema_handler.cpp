#include "elog_sys_schema_handler.h"

#include "elog_file_target.h"
#include "elog_syslog_target.h"
#include "elog_system.h"

namespace elog {

ELogTarget* ELogSysSchemaHandler::loadTarget(const std::string& logTargetCfg,
                                             const ELogTargetSpec& logTargetSpec) {
    std::string name;
    if (logTargetSpec.m_props.size() > 1) {
        ELogSystem::reportError(
            "Invalid log target specification, \'sys\' schema can have at most one property: %s",
            logTargetCfg.c_str());
        return nullptr;
    }
    if (logTargetSpec.m_props.size() == 1) {
        if (logTargetSpec.m_props.begin()->first.compare("name") != 0) {
            ELogSystem::reportError(
                "Invalid log target specification, \'sys\' schema can specify only name property: "
                "%s",
                logTargetCfg.c_str());
            return nullptr;
        }
        name = logTargetSpec.m_props.begin()->second;
        if (name.empty()) {
            ELogSystem::reportError(
                "Invalid log target specification, name property missing value: %s",
                logTargetCfg.c_str());
            return nullptr;
        }
    }

    ELogTarget* logTarget = nullptr;
    if (logTargetSpec.m_path.compare("stderr") == 0) {
        logTarget = new (std::nothrow) ELogFileTarget(stderr);
        if (logTarget == nullptr) {
            ELogSystem::reportError("Failed to create log target, out of memory");
            return nullptr;
        }
    }
    if (logTargetSpec.m_path.compare("stdout") == 0) {
        logTarget = new (std::nothrow) ELogFileTarget(stdout);
        if (logTarget == nullptr) {
            ELogSystem::reportError("Failed to create log target, out of memory");
            return nullptr;
        }
    }
    if (logTargetSpec.m_path.compare("syslog") == 0) {
#ifdef ELOG_LINUX
        logTarget = new (std::nothrow) ELogSysLogTarget();
        if (logTarget == nullptr) {
            ELogSystem::reportError("Failed to create log target, out of memory");
            return nullptr;
        }
#else
        ELogSystem::reportError(
            "Cannot create syslog log target, not supported on current platform");
#endif
    }

    if (logTarget == nullptr) {
        return nullptr;
    }
    if (!name.empty()) {
        logTarget->setName(name.c_str());
    }
    return logTarget;
}

}  // namespace elog
