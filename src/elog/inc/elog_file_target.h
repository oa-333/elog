#ifndef __FILE_LOG_TARGET_H__
#define __FILE_LOG_TARGET_H__

#include <cstdio>

#include "elog_def.h"
#include "elog_target.h"

namespace elog {

class ELOG_API ELogFileTarget : public ELogTarget {
public:
    ELogFileTarget(const char* filePath, ELogFlushPolicy* flushPolicy = nullptr);
    ELogFileTarget(FILE* fileHandle, ELogFlushPolicy* flushPolicy = nullptr)
        : ELogTarget("file", flushPolicy), m_fileHandle(fileHandle), m_shouldClose(false) {
        setAddNewLine(true);
    }
    ELogFileTarget(const ELogFileTarget&) = delete;
    ELogFileTarget(ELogFileTarget&&) = delete;

    ~ELogFileTarget() final;

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
    FILE* m_fileHandle;
    bool m_shouldClose;
};

}  // namespace elog

#endif  // __FILE_LOG_TARGET_H__