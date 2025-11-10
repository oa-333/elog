#ifndef __SYS_LOG_TARGET_H__
#define __SYS_LOG_TARGET_H__

#include <cstdio>

#include "elog_def.h"

#ifdef ELOG_LINUX
#include "elog_target.h"

namespace elog {

class ELogSysLogTarget : public ELogTarget {
public:
    ELogSysLogTarget(bool enableStats = false) : ELogTarget("syslog", nullptr, enableStats) {
        setName("syslog");
    }
    ELogSysLogTarget(const ELogSysLogTarget&) = delete;
    ELogSysLogTarget(ELogSysLogTarget&&) = delete;

    ELOG_DECLARE_LOG_TARGET(ELogSysLogTarget)

protected:
    /** @brief Order the log target to start (required for threaded targets). */
    bool startLogTarget() final;

    /** @brief Order the log target to stop (required for threaded targets). */
    bool stopLogTarget() final;

    /**
     * @brief Order the log target to write a log record (thread-safe).
     * @param logRecord The log record to write to the log target.
     * @param bytesWritten The number of bytes written to log.
     * @return The operation's result.
     */
    bool writeLogRecord(const ELogRecord& logRecord, uint64_t& bytesWritten) final;

    /** @brief Orders a buffered log target to flush it log messages. */
    bool flushLogTarget() final;

private:
    bool logLevelToSysLevel(ELogLevel logLevel, int& sysLevel);
};

}  // namespace elog

#endif  // ELOG_LINUX

#endif  // __SYS_LOG_TARGET_H__