#ifndef __ELOG_BUFFERED_FILE_WRITER_H__
#define __ELOG_BUFFERED_FILE_WRITER_H__

#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

#include "elog_def.h"
#include "elog_stats.h"

namespace elog {

/** @def The hard limit for the buffered file writer's buffer size (in bytes). */
#define ELOG_MAX_FILE_BUFFER_BYTES (64 * 1024 * 1024)

/** @def Default buffers size. */
#define ELOG_DEFAULT_FILE_BUFFER_SIZE_BYTES (1024 * 1024)

struct ELOG_API ELogBufferedStats : public ELogStats {
    ELogBufferedStats() {}
    ELogBufferedStats(const ELogBufferedStats&) = delete;
    ELogBufferedStats(ELogBufferedStats&&) = delete;
    ELogBufferedStats& operator=(const ELogBufferedStats&) = delete;
    ~ELogBufferedStats() final {}

    /** @brief Initializes the statistics variable. */
    bool initialize(uint32_t maxThreads) override;

    /** @brief Terminates the statistics variable. */
    void terminate() override;

    inline void incrementBufferWriteCount() { m_bufferWriteCount.add(getSlotId(), 1); }
    inline void addBufferBytesCount(uint64_t bytes) { m_bufferByteCount.add(getSlotId(), bytes); }

    /**
     * @brief Prints statistics to log.
     * @param logLevel Print log level.
     * @param msg Any title message that would precede the report.
     */
    void toString(ELogBuffer& buffer, ELogTarget* logTarget, const char* msg = "") override;

    /** @brief Allow accumulation (required by segmented log target). */
    void addStats(const ELogBufferedStats& stats) {
        m_bufferWriteCount.addVar(stats.m_bufferByteCount);
        m_bufferByteCount.addVar(stats.m_bufferByteCount);
    }

    inline const ELogStatVar& getBufferWriteCount() const { return m_bufferWriteCount; }
    inline const ELogStatVar& getBufferByteCount() const { return m_bufferByteCount; }

    /** @brief Releases the statistics slot for the current thread. */
    void resetThreadCounters(uint64_t slotId) override;

private:
    /** @brief The total number of writing buffered log data to file/transport-layer. */
    ELogStatVar m_bufferWriteCount;

    /** @brief The total number of buffered bytes written to log. */
    ELogStatVar m_bufferByteCount;
};

/** @brief A utility class for writing data to file with internal buffering. */
class ELOG_API ELogBufferedFileWriter {
public:
    /**
     * @brief Construct a new ELogBufferedFileWriter object.
     * @param bufferSizeBytes The size of the buffer in bytes to use when writing data. Buffer size
     * exceeding the allowed maximum will be truncated. Specify zero size to use default.
     * @param useLock Specifies whether to use locking. When buffering is enabled, a lock is
     * required in multi-threaded scenario, and failing to use a lock will result in undefined
     * behavior. If the buffering is disabled, then normally locking is not required, even with
     * multi-threaded scenarios, unless the caller wishes to avoid log messages from different
     * threads being intermixed in the resulting log file.
     * @note Data is written to the buffer until buffer is full, in which case the buffer is fully
     * written into file with as least system calls as possible.
     * @note Log messages are not split between buffers. This means that if a log message is too
     * large to fit within the free space left in the buffer, then the buffer if first drained to
     * file, then the log message is appended to the buffer. If the log message is larger than the
     * buffer size, then it is written directly to file without buffering.
     */
    ELogBufferedFileWriter(uint64_t bufferSizeBytes, bool useLock)
        : m_fd(0),
          m_bufferSizeBytes(bufferSizeBytes),
          m_bufferOffset(0),
          m_useLock(useLock),
          m_stats(nullptr),
          m_enableStats(true) {
        if (m_bufferSizeBytes > ELOG_MAX_FILE_BUFFER_BYTES) {
            m_bufferSizeBytes = ELOG_MAX_FILE_BUFFER_BYTES;
        } else if (m_bufferSizeBytes == 0) {
            m_bufferSizeBytes = ELOG_DEFAULT_FILE_BUFFER_SIZE_BYTES;
        }
    }
    ELogBufferedFileWriter(const ELogBufferedFileWriter&) = delete;
    ELogBufferedFileWriter(ELogBufferedFileWriter&&) = delete;
    ELogBufferedFileWriter& operator=(const ELogBufferedFileWriter&) = delete;
    ~ELogBufferedFileWriter() {}

    /**
     * @brief Sets the buffered file writer.
     * @param fileHandle The handle of the file into which data is to be written.
     */
    void setFileHandle(FILE* fileHandle);

    /** @brief Pass a reference to the statistics object by the controlling object. */
    inline void setStats(ELogBufferedStats* stats) { m_stats = stats; }

    /** @brief Retrieves the currently set statistics object.  */
    inline ELogBufferedStats* getStats() { return m_stats; }

    /** @brief Disable usage of statistics. */
    inline void disableStats() { m_enableStats = false; }

    /**
     * @brief Write a log message to the log file.
     * @param formattedLogMsg The log message to write.
     * @param length The message size (not including terminating null).
     * @return true If the operation succeeded, otherwise false.
     */
    bool logMsg(const char* formattedLogMsg, size_t length);

    /** @brief Flush current buffer contents to the log (no file flushing). */
    bool flushLogBuffer();

private:
    int m_fd;
    uint64_t m_bufferSizeBytes;
    uint64_t m_bufferOffset;
    alignas(8) std::vector<char> m_logBuffer;
    bool m_useLock;
    alignas(8) std::mutex m_lock;
    ELogBufferedStats* m_stats;
    bool m_enableStats;

    bool logMsgUnlocked(const char* formattedLogMsg, size_t length);

    bool writeToFile(const char* buffer, size_t length);
};

}  // namespace elog

#endif  // __ELOG_BUFFERED_FILE_WRITER_H__