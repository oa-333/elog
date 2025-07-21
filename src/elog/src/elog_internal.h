#ifndef __ELOG_INTERNAL_H__
#define __ELOG_INTERNAL_H__

#include <string>

#include "elog_buffer.h"
#include "elog_common_def.h"
#include "elog_def.h"
#include "elog_record.h"

namespace elog {

/**
 * @brief Formats a log message, using the installed log formatter.
 * @param logRecord The log record to format.
 * @param[out] logMsg The resulting formatted log message.
 */
extern ELOG_API void formatLogMsg(const ELogRecord& logRecord, std::string& logMsg);

/**
 * @brief Formats a log message, using the installed log formatter.
 * @param logRecord The log record to format.
 * @param[out] logBuffer The resulting formatted log buffer.
 */
extern ELOG_API void formatLogBuffer(const ELogRecord& logRecord, ELogBuffer& logBuffer);

/**
 * @brief Logs a log record. In essence to log record is sent to all registered log targets.
 * @param logRecord The lgo record to process.
 * @param logTargetAffinityMask Optionally restricts the message to be directed to a specific
 * log target.
 */
extern ELOG_API void logMsg(
    const ELogRecord& logRecord,
    ELogTargetAffinityMask logTargetAffinityMask = ELOG_ALL_TARGET_AFFINITY_MASK);

}  // namespace elog

#endif  // __ELOG_INTERNAL_H__