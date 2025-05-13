#ifndef __ELOG_BUFFERED_FILE_LOG_TARGET_H__
#define __ELOG_BUFFERED_FILE_LOG_TARGET_H__

#include <cstdio>

#include "elog_buffered_file_writer.h"
#include "elog_def.h"
#include "elog_target.h"

namespace elog {

class ELOG_API ELogBufferedFileTarget : public ELogTarget {
public:
    /**
     * @brief Construct a new ELogBufferedFileTarget object.
     * @param filePath The path to the log file.
     * @param bufferSize The buffer size to use. Cannot be zero.
     * @param useLock Specifies whether to use lock. If buffering is used in a multi-threaded
     * scenario, then a lock is required, and without a lock behavior is undefined. If buffering is
     * disabled, then lock is not required, unless it is desired to avoid log messages from
     * different threads getting occasionally intermixed.
     * @param flushPolicy Optional flush policy to use.
     * @see @ref ELofBufferedFileWriter.
     */
    ELogBufferedFileTarget(const char* filePath, uint32_t bufferSize = 0, bool useLock = true,
                           ELogFlushPolicy* flushPolicy = nullptr);

    /**
     * @brief Construct a new ELogBufferedFileTarget object using an existing file handle.
     * @note This constructor is usually used for logging to the standard error and output streams.
     * @param fileHandle The open file handle to use.
     * @param bufferSize The buffer size to use. Specify zero to disable buffering.
     * @param useLock Specifies whether to use lock. If buffering is used in a multi-threaded
     * scenario, the a lock is required, and without a lock behavior is undefined.
     * @param flushPolicy Optional flush policy to use.
     * @see @ref ELofBufferedFileWriter.
     */
    ELogBufferedFileTarget(FILE* fileHandle, uint32_t bufferSize = 0, bool useLock = true,
                           ELogFlushPolicy* flushPolicy = nullptr)
        : ELogTarget("buffered-file", flushPolicy),
          m_fileWriter(bufferSize, useLock),
          m_fileHandle(nullptr),
          m_shouldClose(false) {
        setAddNewLine(true);
    }
    ELogBufferedFileTarget(const ELogBufferedFileTarget&) = delete;
    ELogBufferedFileTarget(ELogBufferedFileTarget&&) = delete;

    ~ELogBufferedFileTarget() final {}

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
    std::string m_filePath;
    ELogBufferedFileWriter m_fileWriter;
    FILE* m_fileHandle;
    bool m_shouldClose;
};

}  // namespace elog

#endif  // __ELOG_BUFFERED_FILE_LOG_TARGET_H__