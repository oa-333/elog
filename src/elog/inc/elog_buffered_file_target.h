#ifndef __ELOG_BUFFERED_FILE_LOG_TARGET_H__
#define __ELOG_BUFFERED_FILE_LOG_TARGET_H__

#include <cstdio>

#include "elog_buffered_file_writer.h"
#include "elog_target.h"

namespace elog {

class ELOG_API ELogBufferedFileTarget : public ELogTarget {
public:
    /**
     * @brief Construct a new ELogBufferedFileTarget object.
     * @param filePath The path to the log file.
     * @param bufferSizeBytes The buffer size to use. Cannot be zero.
     * @param useLock Specifies whether to use lock. If buffering is used in a multi-threaded
     * scenario, then a lock is required, and without a lock behavior is undefined. If buffering is
     * disabled, then lock is not required, unless it is desired to avoid log messages from
     * different threads getting occasionally intermixed.
     * @param flushPolicy Optional flush policy to use.
     * @see @ref ELofBufferedFileWriter.
     */
    ELogBufferedFileTarget(const char* filePath,
                           uint64_t bufferSizeBytes = ELOG_DEFAULT_FILE_BUFFER_SIZE_BYTES,
                           bool useLock = true, ELogFlushPolicy* flushPolicy = nullptr);

    /**
     * @brief Construct a new ELogBufferedFileTarget object using an existing file handle.
     * @note This constructor is usually used for logging to the standard error and output streams.
     * @param fileHandle The open file handle to use.
     * @param bufferSizeBytes The buffer size to use. Specify zero to default buffer size.
     * @param useLock Specifies whether to use lock. If buffering is used in a multi-threaded
     * scenario, the a lock is required, and without a lock behavior is undefined.
     * @param flushPolicy Optional flush policy to use.
     * @param shouldClose Optionally specify whether the file handle should be closed when done.
     * @see @ref ELofBufferedFileWriter.
     */
    ELogBufferedFileTarget(FILE* fileHandle,
                           uint32_t bufferSizeBytes = ELOG_DEFAULT_FILE_BUFFER_SIZE_BYTES,
                           bool useLock = true, ELogFlushPolicy* flushPolicy = nullptr,
                           bool shouldClose = false)
        : ELogTarget("buffered-file", flushPolicy),
          m_fileWriter(bufferSizeBytes, useLock),
          m_fileHandle(fileHandle),
          m_shouldClose(shouldClose) {
        if (useLock) {
            setNativelyThreadSafe();
        }
        setAddNewLine(true);
    }
    ELogBufferedFileTarget(const ELogBufferedFileTarget&) = delete;
    ELogBufferedFileTarget(ELogBufferedFileTarget&&) = delete;
    ELogBufferedFileTarget& operator=(const ELogBufferedFileTarget&) = delete;

    ~ELogBufferedFileTarget() final {}

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
    std::string m_filePath;
    ELogBufferedFileWriter m_fileWriter;
    FILE* m_fileHandle;
    bool m_shouldClose;
};

}  // namespace elog

#endif  // __ELOG_BUFFERED_FILE_LOG_TARGET_H__