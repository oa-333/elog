#ifndef __FILE_LOG_TARGET_H__
#define __FILE_LOG_TARGET_H__

#include <cstdio>

#include "elog_def.h"
#include "elog_target.h"

namespace elog {

class ELOG_API ELogFileTarget : public ELogAbstractTarget {
public:
    ELogFileTarget(const char* filePath, ELogFlushPolicy* flushPolicy);
    ELogFileTarget(FILE* fileHandle, ELogFlushPolicy* flushPolicy)
        : ELogAbstractTarget(flushPolicy), m_fileHandle(fileHandle), m_shouldClose(false) {}
    ELogFileTarget(const ELogFileTarget&) = delete;
    ELogFileTarget(ELogFileTarget&&) = delete;

    ~ELogFileTarget() final;

    /** @brief Order the log target to start (required for threaded targets). */
    bool start() final;

    /** @brief Order the log target to stop (required for threaded targets). */
    bool stop() final;

    /** @brief Orders a buffered log target to flush it log messages. */
    void flush() final;

protected:
    /** @brief Log a formatted message. */
    void log(const std::string& formattedLogMsg) final;

private:
    std::string m_filePath;
    FILE* m_fileHandle;
    bool m_shouldClose;
};

}  // namespace elog

#endif  // __FILE_LOG_TARGET_H__