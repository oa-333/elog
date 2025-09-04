#ifndef __SEGMENTED_FILE_LOG_TARGET_H__
#define __SEGMENTED_FILE_LOG_TARGET_H__

#include <atomic>
#include <list>
#include <string>

#include "elog_concurrent_ring_buffer.h"
#include "elog_rolling_bitset.h"
#include "elog_target.h"
#include "file/elog_buffered_file_writer.h"

namespace elog {

/** @def Maximum value allowed for segment limit (bytes). Currently totalling in 4GB. */
#define ELOG_MAX_SEGMENT_LIMIT_BYTES (4ull * 1024ull * 1024ull * 1024ull)

/** @def Maximum value allowed for segment ring size. Currently totalling in 64 million items. */
#define ELOG_MAX_SEGMENT_RING_SIZE (64ul * 1024ul * 1024ul)

/** @def Maximum value allowed for segment count (for rotating log target). */
#define ELOG_MAX_SEGMENT_COUNT (1024ul * 1024ul)

/** @def The default ring buffer size used for pending messages during segment switch. */
#define ELOG_DEFAULT_SEGMENT_RING_SIZE (1024ul * 1024ul)

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
     * @param segmentLimitBytes The maximum segment size in bytes.
     * @param segmentRingSize Optional size of the pending message ring buffer used
     * during segment switch.
     * @param segmentCount Optionally specify the maximum number of segments to use. This will cause
     * log segments to rotate. By default no log rotation takes place.
     * @param fileBufferSizeBytes Optionally specify file buffer size to use. This will cause the
     * segmented logger to use ELog's internal implementation of buffered file, which is slightly
     * better than default fopen/fwrite functions. Specify zero to disable buffering. By default
     * file buffering is not used.
     * @param flushPolicy Optional flush policy to be used in conjunction with this log target.
     * @param enableStats Specifies whether log target statistics should be collected.
     */
    ELogSegmentedFileTarget(const char* logPath, const char* logName, uint64_t segmentLimitBytes,
                            uint32_t segmentRingSize = ELOG_DEFAULT_SEGMENT_RING_SIZE,
                            uint64_t fileBufferSizeBytes = 0, uint32_t segmentCount = 0,
                            ELogFlushPolicy* flushPolicy = nullptr, bool enableStats = true);
    ELogSegmentedFileTarget(const ELogSegmentedFileTarget&) = delete;
    ELogSegmentedFileTarget(ELogSegmentedFileTarget&&) = delete;
    ELogSegmentedFileTarget& operator=(const ELogSegmentedFileTarget&) = delete;

    ~ELogSegmentedFileTarget() final;

protected:
    /** @brief Log a formatted message. */
    void logFormattedMsg(const char* formattedLogMsg, size_t length) final;

    /** @brief Order the log target to start (required for threaded targets). */
    bool startLogTarget() final;

    /** @brief Order the log target to stop (required for threaded targets). */
    bool stopLogTarget() final;

    /** @brief Orders a buffered log target to flush it log messages. */
    bool flushLogTarget() final;

    /** @brief Creates a statistics object. */
    ELogStats* createStats() override;

private:
    // use lock-free scalable ring buffer for saving pending messages during segment switch
    typedef ELogConcurrentRingBuffer<std::string> LogMsgQueue;

    // single segment data
    struct SegmentData {
        uint32_t m_segmentId;
        std::atomic<uint64_t> m_bytesLogged;
        FILE* m_segmentFile;
        ELogBufferedFileWriter* m_bufferedFileWriter;
        LogMsgQueue m_pendingMsgs;
        ELogBufferedStats m_stats;

        SegmentData(uint32_t segmentId, uint64_t bytesLogged = 0)
            : m_segmentId(segmentId),
              m_bytesLogged(bytesLogged),
              m_segmentFile(nullptr),
              m_bufferedFileWriter(nullptr) {}
        SegmentData(const SegmentData&) = delete;
        SegmentData(SegmentData&&) = delete;
        SegmentData& operator=(const SegmentData&) = delete;
        ~SegmentData() { m_pendingMsgs.terminate(); }

        bool open(const char* segmentPath, uint64_t fileBufferSizeBytes = 0, bool useLock = true,
                  bool truncateSegment = false, bool enableStats = true);
        bool log(const char* logMsg, size_t len);
        bool drain();
        bool flush();
        bool close();
    };

    struct SegmentedStats : public ELogStats {
        SegmentedStats() {}
        SegmentedStats(const SegmentedStats&) = delete;
        SegmentedStats(SegmentedStats&&) = delete;
        SegmentedStats& operator=(const SegmentedStats&) = delete;
        ~SegmentedStats() final {}

        bool initialize(uint32_t maxThreads) override;

        void terminate() override;

        inline void incrementSegmentCount() { m_segmentCount.add(getSlotId(), 1); }
        inline void incrementOpenSegmentFailCount() { m_openSegmentFailCount.add(getSlotId(), 1); }
        inline void incrementCloseSegmentFailCount() {
            m_closeSegmentFailCount.add(getSlotId(), 1);
        }
        inline void addCloseSegmentBytes(uint64_t bytes) {
            m_closedSegmentBytes.add(getSlotId(), bytes);
        }
        inline void addPendingMsgCount(uint64_t count) {
            m_pendingMsgCount.add(getSlotId(), count);
        }
        inline void addBufferedStats(const ELogBufferedStats& stats) {
            m_bufferedStats.addStats(stats);
        }

        /**
         * @brief Prints statistics to an output string buffer.
         * @param buffer The output string buffer.
         * @param logTarget The log target whose statistics are to be printed.
         * @param msg Any title message that would precede the report.
         */
        void toString(ELogBuffer& buffer, ELogTarget* logTarget, const char* msg = "") override;

        /** @brief Releases the statistics slot for the current thread. */
        void resetThreadCounters(uint64_t slotId) override;

    private:
        /** @brief Total number of segments used. */
        ELogStatVar m_segmentCount;

        /** @brief Total number of failures to open new segment. */
        ELogStatVar m_openSegmentFailCount;

        /** @brief Total number of failures to close a log segment file. */
        ELogStatVar m_closeSegmentFailCount;

        /** @brief Total number of bytes used in full segments. */
        ELogStatVar m_closedSegmentBytes;

        /** @brief Total number of messages queued for logging during segment switch. */
        ELogStatVar m_pendingMsgCount;

        /**
         * @brief Optional accumulated bufferring statistics from each segment, in case buffer is
         * used.
         */
        ELogBufferedStats m_bufferedStats;
    };

    struct SegmentInfo {
        std::string m_fileName;  // without containing folder
        uint32_t m_segmentId;
        uint64_t m_fileSizeBytes;
        uint64_t m_lastModifyTime;
    };

    uint64_t m_segmentLimitBytes;
    uint64_t m_fileBufferSizeBytes;
    uint32_t m_segmentRingSize;
    uint32_t m_segmentCount;
    std::atomic<SegmentData*> m_currentSegment;
    std::atomic<uint64_t> m_epoch;
    ELogRollingBitset m_epochSet;
    std::string m_logPath;
    std::string m_logName;
    SegmentedStats* m_segmentedStats;

    bool openSegment();
    bool openRotatingSegment();
    bool getSegmentInfo(std::vector<SegmentInfo>& segmentInfo);
    bool getSegmentCount(uint32_t& segmentCount, uint64_t& lastSegmentSizeBytes);
    bool scanDirFiles(const char* dirPath, std::vector<std::string>& fileNames);
    bool getSegmentIndex(const std::string& fileName, uint32_t& segmentIndex);
    bool getFileSize(const char* filePath, uint64_t& fileSize);
    bool getFileTime(const char* filePath, uint64_t& fileTime);
    void formatSegmentPath(std::string& segmentPath, uint32_t segmentId);
    bool advanceSegment(uint32_t segmentId, const std::string& logMsg, uint64_t currentEpoch);
    void logMsgQueue(std::list<std::string>& logMsgs, FILE* segmentFile);
};

}  // namespace elog

#endif  // __SEGMENTED_FILE_LOG_TARGET_H__