#ifndef __SYS_LOG_TARGET_H__
#define __SYS_LOG_TARGET_H__

#include <cstdio>

#include "elog_def.h"

#ifdef ELOG_LINUX
#include "elog_target.h"

namespace elog {

class ELOG_API ELogSysLogTarget : public ELogTarget {
public:
    ELogSysLogTarget() : ELogTarget("syslog") {}
    ELogSysLogTarget(const ELogSysLogTarget&) = delete;
    ELogSysLogTarget(ELogSysLogTarget&&) = delete;

    ~ELogSysLogTarget() final {}

    /** @brief Order the log target to start (required for threaded targets). */
    bool start() final;

    /** @brief Order the log target to stop (required for threaded targets). */
    bool stop() final;

    /** @brief Sends a log record to a log target. */
    void log(const ELogRecord& logRecord) final;

    /** @brief Orders a buffered log target to flush it log messages. */
    void flush() final;

private:
    bool logLevelToSysLevel(ELogLevel logLevel, int& sysLevel);
};

}  // namespace elog

#endif  // ELOG_LINUX

#endif  // __SYS_LOG_TARGET_H__