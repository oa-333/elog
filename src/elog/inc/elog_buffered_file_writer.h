#ifndef __ELOG_BUFFERED_FILE_WRITER_H__
#define __ELOG_BUFFERED_FILE_WRITER_H__

#include <elog_def.h>

#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

namespace elog {

/** @def The hard limit for the buffered file writer's buffer size (in bytes). */
#define ELOG_MAX_FILE_BUFFER_BYTES (64 * 1024 * 1024)

/** @def Default buffers size. */
#define ELOG_DEFAULT_FILE_BUFFER_SIZE_BYTES (1024 * 1024)

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
        : m_fd(0), m_bufferSizeBytes(bufferSizeBytes), m_bufferOffset(0), m_useLock(useLock) {
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

    bool logMsgUnlocked(const char* formattedLogMsg, size_t length);

    bool writeToFile(const char* buffer, size_t length);
};

}  // namespace elog

#endif  // __ELOG_BUFFERED_FILE_WRITER_H__