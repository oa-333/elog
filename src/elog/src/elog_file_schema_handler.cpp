#include "elog_file_schema_handler.h"

#include "elog_buffered_file_target.h"
#include "elog_common.h"
#include "elog_config_loader.h"
#include "elog_error.h"
#include "elog_file_target.h"
#include "elog_segmented_file_target.h"

namespace elog {

static const char* LOG_SUFFIX = ".log";

ELogTarget* ELogFileSchemaHandler::loadTarget(const std::string& logTargetCfg,
                                              const ELogTargetSpec& logTargetSpec) {
    // path should be already parsed
    if (logTargetSpec.m_path.empty()) {
        ELOG_REPORT_ERROR("Invalid log target specification, scheme 'file' requires a path: %s",
                          logTargetCfg.c_str());
        return nullptr;
    }

    // there could be optional property file_buffer_size
    uint32_t bufferSize = 0;
    ELogPropertyMap::const_iterator itr = logTargetSpec.m_props.find("file_buffer_size");
    if (itr != logTargetSpec.m_props.end()) {
        if (!parseIntProp("file_buffer_size", logTargetCfg, itr->second, bufferSize)) {
            return nullptr;
        }
    }

    // there could be an optional property file-no-lock
    // NOTE: Since file lock is relevant only for buffered file logging, the default value for
    // file_lock is true, assuming that multi-threaded scenario is the common use case, so unless
    // user specifies explicitly otherwise, locking is used.
    bool useFileLock = true;
    itr = logTargetSpec.m_props.find("file_lock");
    if (itr != logTargetSpec.m_props.end()) {
        if (!parseBoolProp("file_lock", logTargetCfg, itr->second, useFileLock)) {
            return nullptr;
        }
    }

    // there could be optional property file_segment_size_mb
    uint32_t segmentSizeMB = 0;
    itr = logTargetSpec.m_props.find("file_segment_size_mb");
    if (itr != logTargetSpec.m_props.end()) {
        if (!parseIntProp("file_segment_size_mb", logTargetCfg, itr->second, segmentSizeMB)) {
            return nullptr;
        }
    }

    // there could be optional property file_segment_ring_size
    uint64_t segmentRingSize = ELOG_DEFAULT_SEGMENT_RING_SIZE;
    itr = logTargetSpec.m_props.find("file_segment_ring_size");
    if (itr != logTargetSpec.m_props.end()) {
        if (!parseIntProp("file_segment_ring_size", logTargetCfg, itr->second, segmentRingSize)) {
            return nullptr;
        }
    }

    return createLogTarget(logTargetSpec.m_path, bufferSize, useFileLock, segmentSizeMB,
                           segmentRingSize);
}

ELogTarget* ELogFileSchemaHandler::loadTarget(const std::string& logTargetCfg,
                                              const ELogTargetNestedSpec& targetNestedSpec) {
    // first make sure there ar no log target sub-specs
    if (targetNestedSpec.m_subSpec.find("log_target") != targetNestedSpec.m_subSpec.end()) {
        ELOG_REPORT_ERROR("File log target cannot have sub-targets: %s", logTargetCfg.c_str());
        return nullptr;
    }

    // no difference, just call URL style loading
    return loadTarget(logTargetCfg, targetNestedSpec.m_spec);
}

ELogTarget* ELogFileSchemaHandler::loadTarget(const ELogConfigMapNode* logTargetCfg) {
    // path should be already parsed
    std::string path;
    if (!ELogConfigLoader::getLogTargetStringProperty(logTargetCfg, "file", "path", path)) {
        return nullptr;
    }

    // there could be optional property file_buffer_size
    int64_t bufferSize = 0;
    if (!ELogConfigLoader::getOptionalLogTargetIntProperty(logTargetCfg, "file", "file_buffer_size",
                                                           bufferSize)) {
        return nullptr;
    }

    // there could be an optional property file_lock
    // NOTE: Since file lock is relevant only for buffered file logging, the default value for
    // file_lock is true, assuming that multi-threaded scenario is the common use case, so unless
    // user specifies explicitly otherwise, locking is used.
    bool useFileLock = true;
    if (!ELogConfigLoader::getOptionalLogTargetBoolProperty(logTargetCfg, "file", "file_lock",
                                                            useFileLock)) {
        return nullptr;
    }

    // there could be optional property file_segment_size_mb
    int64_t segmentSizeMB = 0;
    if (!ELogConfigLoader::getOptionalLogTargetIntProperty(logTargetCfg, "file",
                                                           "file_segment_size_mb", segmentSizeMB)) {
        return nullptr;
    }

    // there could be optional property file_segment_ring_size
    int64_t segmentRingSize = ELOG_DEFAULT_SEGMENT_RING_SIZE;
    if (!ELogConfigLoader::getOptionalLogTargetIntProperty(
            logTargetCfg, "file", "file_segment_ring_size", segmentRingSize)) {
        return nullptr;
    }

    return createLogTarget(path, bufferSize, useFileLock, segmentSizeMB, segmentRingSize);
}

ELogTarget* ELogFileSchemaHandler::createLogTarget(const std::string& path, int64_t bufferSize,
                                                   bool useFileLock, int64_t segmentSizeMB,
                                                   int64_t segmentRingSize) {
    ELogTarget* logTarget = nullptr;
    if (segmentSizeMB > 0) {
        std::string::size_type lastSlashPos = path.find_last_of("\\/");
        /// assuming segmented log is to be created in current folder, and path is the file name
        if (lastSlashPos == std::string::npos) {
            logTarget = new (std::nothrow)
                ELogSegmentedFileTarget("", path.c_str(), segmentSizeMB, segmentRingSize);
        } else {
            std::string logPath = path.substr(0, lastSlashPos);
            std::string logName = path.substr(lastSlashPos + 1);
            if (logName.ends_with(LOG_SUFFIX)) {
                logName = logName.substr(0, logName.size() - strlen(LOG_SUFFIX));
            }
            logTarget = new (std::nothrow) ELogSegmentedFileTarget(logPath.c_str(), logName.c_str(),
                                                                   segmentSizeMB, segmentRingSize);
        }
    } else {
        if (bufferSize > 0) {
            logTarget =
                new (std::nothrow) ELogBufferedFileTarget(path.c_str(), bufferSize, useFileLock);
        } else {
            logTarget = new (std::nothrow) ELogFileTarget(path.c_str());
        }
    }

    return logTarget;
}

}  // namespace elog
