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

    /** @brief Sends a log record to a log target. */
    uint32_t writeLogRecord(const ELogRecord& logRecord) final;

    /** @brief Orders a buffered log target to flush it log messages. */
    bool flushLogTarget() final;

private:
    bool logLevelToSysLevel(ELogLevel logLevel, int& sysLevel);
};

}  // namespace elog

#endif  // ELOG_LINUX

#endif  // __SYS_LOG_TARGET_H__