#include "elog_file_schema_handler.h"

#include "elog_buffered_file_target.h"
#include "elog_common.h"
#include "elog_config_loader.h"
#include "elog_error.h"
#include "elog_file_target.h"
#include "elog_segmented_file_target.h"

namespace elog {

static const char* LOG_SUFFIX = ".log";

ELogTarget* ELogFileSchemaHandler::loadTarget(const ELogConfigMapNode* logTargetCfg) {
    // path should be already parsed
    std::string path;
    if (!ELogConfigLoader::getLogTargetStringProperty(logTargetCfg, "file", "path", path)) {
        return nullptr;
    }

    // there could be optional property file_buffer_size
    uint32_t bufferSize = 0;
    if (!ELogConfigLoader::getOptionalLogTargetUInt32Property(logTargetCfg, "file",
                                                              "file_buffer_size", bufferSize)) {
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
    uint32_t segmentSizeMB = 0;
    if (!ELogConfigLoader::getOptionalLogTargetUInt32Property(
            logTargetCfg, "file", "file_segment_size_mb", segmentSizeMB)) {
        return nullptr;
    }

    // there could be optional property file_segment_ring_size
    uint32_t segmentRingSize = ELOG_DEFAULT_SEGMENT_RING_SIZE;
    if (!ELogConfigLoader::getOptionalLogTargetUInt32Property(
            logTargetCfg, "file", "file_segment_ring_size", segmentRingSize)) {
        return nullptr;
    }

    // finally, there could be optional property file_segment_count to specify rotation
    uint32_t segmentCount = 0;
    if (!ELogConfigLoader::getOptionalLogTargetUInt32Property(logTargetCfg, "file",
                                                              "file_segment_count", segmentCount)) {
        return nullptr;
    }

    return createLogTarget(path, bufferSize, useFileLock, segmentSizeMB, segmentRingSize,
                           segmentCount);
}

ELogTarget* ELogFileSchemaHandler::createLogTarget(const std::string& path, uint32_t bufferSize,
                                                   bool useFileLock, uint32_t segmentSizeMB,
                                                   uint32_t segmentRingSize,
                                                   uint32_t segmentCount) {
    // when invoked from ELogSystem, the ring size is zero, so we fix it to default value
    if (segmentRingSize == 0) {
        segmentRingSize = ELOG_DEFAULT_SEGMENT_RING_SIZE;
    }

    ELogTarget* logTarget = nullptr;
    if (segmentSizeMB > 0) {
        std::string::size_type lastSlashPos = path.find_last_of("\\/");
        // assuming segmented log is to be created in current folder, and path is the file name
        if (lastSlashPos == std::string::npos) {
            logTarget = new (std::nothrow) ELogSegmentedFileTarget(
                "", path.c_str(), segmentSizeMB, segmentRingSize, bufferSize, segmentCount);
        } else {
            std::string logPath = path.substr(0, lastSlashPos);
            std::string logName = path.substr(lastSlashPos + 1);
            if (logName.ends_with(LOG_SUFFIX)) {
                logName = logName.substr(0, logName.size() - strlen(LOG_SUFFIX));
            }
            logTarget = new (std::nothrow)
                ELogSegmentedFileTarget(logPath.c_str(), logName.c_str(), segmentSizeMB,
                                        segmentRingSize, bufferSize, segmentCount);
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
