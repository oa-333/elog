#ifndef __ELOG_BUFFERED_FILE_WRITER_H__
#define __ELOG_BUFFERED_FILE_WRITER_H__

#include <elog_def.h>

#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

namespace elog {

/** @brief A utility class for writing data to file with internal buffering. */
class ELOG_API ELogBufferedFileWriter {
public:
    /**
     * @brief Construct a new ELogBufferedFileWriter object.
     * @param bufferSizeBytes The size of the buffer in bytes to use when writing data. Specify zero
     * size to disable buffering altogether.
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
    ELogBufferedFileWriter(uint32_t bufferSizeBytes, bool useLock)
        : m_fd(0), m_bufferSizeBytes(bufferSizeBytes), m_bufferOffset(0), m_useLock(useLock) {}
    ELogBufferedFileWriter(const ELogBufferedFileWriter&) = delete;
    ELogBufferedFileWriter(ELogBufferedFileWriter&&) = delete;
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
    uint32_t m_bufferSizeBytes;
    uint32_t m_bufferOffset;
    std::vector<char> m_logBuffer;
    bool m_useLock;
    alignas(8) std::mutex m_lock;

    bool logMsgUnlocked(const char* formattedLogMsg, size_t length);

    bool writeToFile(const char* buffer, uint32_t length);
};

}  // namespace elog

#endif  // __ELOG_BUFFERED_FILE_WRITER_H__