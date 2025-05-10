#ifndef __SEGMENTED_FILE_LOG_TARGET_H__
#define __SEGMENTED_FILE_LOG_TARGET_H__

#include <atomic>
#include <string>

#include "elog_def.h"
#include "elog_target.h"

// TODO: allow combining segmented file log target with deferred log target
// aso have deferred log target to act on some combined flush policy, such that all queued messages
// are drained during queue switch

namespace elog {

/**
 * @brief A lock-free segmented log file target, that breaks log file into segments by a configured
 * segment size limit. The segmented log file target can be combined with a user specified flush
 * policy. If none-given, then the default (immediate) policy will be used, that is, the current log
 * segment will be flushed after each log message.
 *
 * The segmented log file target logs message and switches segments in a safe lock-free manner,
 * although the logger, on whose log message call a segment switch is performed, will incur the log
 * segment switch overhead (open new segment, switch segments, log message, busy wait until previous
 * segment loggers are finished).
 */
class ELOG_API ELogSegmentedFileTarget : public ELogTarget {
public:
    ELogSegmentedFileTarget(const char* logPath, const char* logName, uint32_t segmentLimitMB,
                            ELogFlushPolicy* flushPolicy);
    ELogSegmentedFileTarget(const ELogSegmentedFileTarget&) = delete;
    ELogSegmentedFileTarget(ELogSegmentedFileTarget&&) = delete;

    ~ELogSegmentedFileTarget() final;

    /** @brief Orders a buffered log target to flush it log messages. */
    void flush() final;

protected:
    /** @brief Log a formatted message. */
    void logFormattedMsg(const std::string& formattedLogMsg) final;

    /** @brief Order the log target to start (required for threaded targets). */
    bool startLogTarget() final;

    /** @brief Order the log target to stop (required for threaded targets). */
    bool stopLogTarget() final;

private:
    std::string m_logPath;
    std::string m_logName;
    uint32_t m_segmentLimitBytes;
    std::atomic<uint32_t> m_segmentCount;
    std::atomic<uint32_t> m_segmentSizeBytes;
    std::atomic<FILE*> m_currentSegment;
    std::atomic<uint64_t> m_entered;
    std::atomic<uint64_t> m_left;

    bool openSegment();
    bool getSegmentCount(uint32_t& segmentCount, uint32_t& lastSegmentSizeBytes);

    bool scanDirFiles(const char* dirPath, std::vector<std::string>& fileNames);
    bool getSegmentIndex(const std::string& fileName, int32_t& segmentIndex);
    bool getFileSize(const char* filePath, uint32_t& fileSize);
    void formatSegmentPath(std::string& segmentPath);
    bool advanceSegment();
};

}  // namespace elog

#endif  // __SEGMENTED_FILE_LOG_TARGET_H__