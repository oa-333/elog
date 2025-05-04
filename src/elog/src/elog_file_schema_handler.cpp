#include "elog_file_schema_handler.h"

#include "elog_file_target.h"
#include "elog_segmented_file_target.h"
#include "elog_system.h"

namespace elog {

ELogTarget* ELogFileSchemaHandler::loadTarget(const std::string& logTargetCfg,
                                              const ELogTargetSpec& logTargetSpec) {
    // path should be already parsed
    if (logTargetSpec.m_path.empty()) {
        ELogSystem::reportError(
            "Invalid log target specification, scheme 'file' requires a path: %s",
            logTargetCfg.c_str());
        return nullptr;
    }

    // there could be optional poperties: segment-size-mb, deferred,
    // queue-batch-size=<batch-size>,queue-timeout-millis=<timeout-millis>
    // quantum-buffer-size=<buffer-size>
    uint32_t segmentSizeMB = 0;
    for (const ELogProperty& prop : logTargetSpec.m_props) {
        // parse segment size property
        if (prop.first.compare("segment-size-mb") == 0) {
            if (segmentSizeMB > 0) {
                ELogSystem::reportError("Segment size can be specified only once: %s",
                                        logTargetCfg.c_str());
                return nullptr;
            }
            if (!parseIntProp("segment-size-mb", logTargetCfg, prop.second, segmentSizeMB)) {
                return nullptr;
            }
        }
    }

    ELogTarget* logTarget = nullptr;
    if (segmentSizeMB > 0) {
#ifdef ELOG_WINDOWS
        std::string::size_type lastSlashPos = logTargetSpec.m_path.find_last_of('\\');
#else
        std::string::size_type lastSlashPos = logTargetSpec.m_path.find_last_of('/');
#endif
        /// assuming segmented log is to be created in current folder, and path is the file name
        if (lastSlashPos == std::string::npos) {
            logTarget = new (std::nothrow)
                ELogSegmentedFileTarget("", logTargetSpec.m_path.c_str(), segmentSizeMB, nullptr);
        } else {
            std::string logPath = logTargetSpec.m_path.substr(0, lastSlashPos);
            std::string logName = logTargetSpec.m_path.substr(lastSlashPos + 1);
            logTarget = new (std::nothrow)
                ELogSegmentedFileTarget(logPath.c_str(), logName.c_str(), segmentSizeMB, nullptr);
        }
    } else {
        logTarget = new (std::nothrow) ELogFileTarget(logTargetSpec.m_path.c_str());
    }

    return logTarget;
}

}  // namespace elog
