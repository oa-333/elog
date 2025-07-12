#ifndef __ELOG_FILE_LOG_TARGET_H__
#define __ELOG_FILE_LOG_TARGET_H__

#include <cstdio>

#include "elog_target.h"

namespace elog {

class ELOG_API ELogFileTarget : public ELogTarget {
public:
    /**
     * @brief Construct a new ELogFileTarget object.
     * @param filePath The path to the log file.
     * @param flushPolicy Optional flush policy to use.
     * @note On Windows/MinGW platforms there is no support stdio unlocked API, so it is advised to
     * use buffered file target instead on those platforms.
     */
    ELogFileTarget(const char* filePath, ELogFlushPolicy* flushPolicy = nullptr);

    /**
     * @brief Construct a new ELogFileTarget object using an existing file handle.
     * @note This constructor is usually used for logging to the standard error and output streams.
     * @param fileHandle The open file handle to use.
     * @param flushPolicy Optional flush policy to use.
     * @param shouldClose Optionally specify whether the file handle should be closed when done.
     * @see @ref ELofBufferedFileWriter.
     */
    ELogFileTarget(FILE* fileHandle, ELogFlushPolicy* flushPolicy = nullptr,
                   bool shouldClose = false);

    ELogFileTarget(const ELogFileTarget&) = delete;
    ELogFileTarget(ELogFileTarget&&) = delete;

    ~ELogFileTarget() final {}

    /** @brief Experimental API for configuring optimal buffer size according to setvbuf(). */
    bool configureOptimalBufferSize();

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
    FILE* m_fileHandle;
    bool m_shouldClose;
};

}  // namespace elog

#endif  // __ELOG_FILE_LOG_TARGET_H__