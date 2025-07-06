#ifndef __SEGMENTED_FILE_LOG_TARGET_H__
#define __SEGMENTED_FILE_LOG_TARGET_H__

#include <atomic>
#include <list>
#include <string>

#include "elog_buffered_file_writer.h"
#include "elog_concurrent_ring_buffer.h"
#include "elog_rolling_bitset.h"
#include "elog_target.h"

namespace elog {

/** @def The default ring buffer size used for pending messages during segment switch. */
#define ELOG_DEFAULT_SEGMENT_RING_SIZE (1024 * 1024)

/**
 * @brief A lock-free segmented log file target, that breaks log file into segments by a configured
 * segment size limit. The segmented log file target can be combined with a user specified flush
 * policy. If none-given, then the no flush policy is used, that is, the current log segment will be
 * flushed according to underlying implementation. Normally that means when internal buffer is full.
 *
 * The segmented log file target logs message and switches segments in a safe lock-free manner.
 * Pay attention that the logger, on whose log-message call a segment switch is performed, will
 * incur the log segment switch overhead (open new segment, switch segments, log message, busy wait
 * until previous segment loggers are finished, log pending messages accumulated during switch).
 */
class ELOG_API ELogSegmentedFileTarget : public ELogTarget {
public:
    /**
     * @brief Construct a new ELogSegmentedFileTarget object
     * @param logPath The path to the directory in which log file segments are to be put.
     * @param logName The base name of the log file segments. This should not include ".log"
     * extension, as it is being automatically added.
     * @param segmentLimitMB The maximum segment size in megabytes.
     * @param segmentRingSize Optional size of the pending message ring buffer used
     * during segment switch.
     * @param segmentCount Optionally specify the maximum number of segments to use. This will cause
     * log segments to rotate. By default no log rotation takes place.
     * @param fileBufferSizeBytes Optionally specify file buffer size to use. This will cause the
     * segmented logger to use ELog's internal implementation of buffered file, which is slightly
     * better than default fopen/fwrite functions. By default file buffering is not used.
     * @param flushPolicy Optional flush policy to be used in conjunction with this log target.
     */
    ELogSegmentedFileTarget(const char* logPath, const char* logName, uint32_t segmentLimitMB,
                            uint64_t segmentRingSize = ELOG_DEFAULT_SEGMENT_RING_SIZE,
                            uint64_t fileBufferSizeBytes = 0, uint64_t segmentCount = 0,
                            ELogFlushPolicy* flushPolicy = nullptr);
    ELogSegmentedFileTarget(const ELogSegmentedFileTarget&) = delete;
    ELogSegmentedFileTarget(ELogSegmentedFileTarget&&) = delete;

    ~ELogSegmentedFileTarget() final;

protected:
    /** @brief Log a formatted message. */
    void logFormattedMsg(const char* formattedLogMsg, size_t length) final;

    /** @brief Order the log target to start (required for threaded targets). */
    bool startLogTarget() final;

    /** @brief Order the log target to stop (required for threaded targets). */
    bool stopLogTarget() final;

    /** @brief Orders a buffered log target to flush it log messages. */
    void flushLogTarget() final;

private:
    // use lock-free scalable ring buffer for saving pending messages during segment switch
    typedef ELogConcurrentRingBuffer<std::string> LogMsgQueue;

    // single segment data
    struct SegmentData {
        uint64_t m_segmentId;
        std::atomic<uint64_t> m_bytesLogged;
        FILE* m_segmentFile;
        ELogBufferedFileWriter* m_bufferedFileWriter;
        LogMsgQueue m_pendingMsgs;

        SegmentData(uint64_t segmentId, uint64_t bytesLogged = 0)
            : m_segmentId(segmentId),
              m_bytesLogged(bytesLogged),
              m_segmentFile(nullptr),
              m_bufferedFileWriter(nullptr) {}
        ~SegmentData() { m_pendingMsgs.terminate(); }

        bool open(const char* segmentPath, uint64_t fileBufferSizeBytes = 0, bool useLock = true,
                  bool truncateSegment = false);
        bool log(const char* logMsg, size_t len);
        bool drain();
        bool flush();
        bool close();
    };

    uint64_t m_segmentLimitBytes;
    uint64_t m_segmentRingSize;
    uint64_t m_fileBufferSizeBytes;
    uint64_t m_segmentCount;
    std::atomic<SegmentData*> m_currentSegment;
    std::atomic<uint64_t> m_epoch;
    ELogRollingBitset m_epochSet;
    std::string m_logPath;
    std::string m_logName;

    bool openSegment();
    bool getSegmentCount(uint32_t& segmentCount, uint32_t& lastSegmentSizeBytes);
    bool scanDirFiles(const char* dirPath, std::vector<std::string>& fileNames);
    bool getSegmentIndex(const std::string& fileName, int32_t& segmentIndex);
    bool getFileSize(const char* filePath, uint32_t& fileSize);
    void formatSegmentPath(std::string& segmentPath, uint32_t segmentId);
    bool advanceSegment(uint32_t segmentId, const std::string& logMsg, uint64_t currentEpoch);
    void logMsgQueue(std::list<std::string>& logMsgs, FILE* segmentFile);
};

}  // namespace elog

#endif  // __SEGMENTED_FILE_LOG_TARGET_H__