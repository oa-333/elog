#ifndef __ELOG_INTERNAL_H__
#define __ELOG_INTERNAL_H__

#include <string>

#include "elog_buffer.h"
#include "elog_common.h"
#include "elog_common_def.h"
#include "elog_def.h"
#include "elog_formatter.h"
#include "elog_record.h"

namespace elog {

/** @brief Retrieves the maximum number of threads configured for ELog. */
extern uint32_t getMaxThreads();

/** @brief Resets the statistics counters for the current thread. */
extern void resetThreadStatCounters(uint64_t slotId);

/**
 * @brief Formats a log message, using the installed log formatter.
 * @param logRecord The log record to format.
 * @param[out] logMsg The resulting formatted log message.
 */
extern void formatLogMsg(const ELogRecord& logRecord, std::string& logMsg);

/**
 * @brief Formats a log message, using the installed log formatter.
 * @param logRecord The log record to format.
 * @param[out] logBuffer The resulting formatted log buffer.
 */
extern void formatLogBuffer(const ELogRecord& logRecord, ELogBuffer& logBuffer);

/** @brief Retrieves the default global log formatter. */
extern ELogFormatter* getDefaultLogFormatter();

/**
 * @brief Logs a log record. In essence to log record is sent to all registered log targets.
 * @param logRecord The lgo record to process.
 * @param logTargetAffinityMask Optionally restricts the message to be directed to a specific
 * log target.
 */
extern void logMsg(const ELogRecord& logRecord,
                   ELogTargetAffinityMask logTargetAffinityMask = ELOG_ALL_TARGET_AFFINITY_MASK);

#ifdef ELOG_ENABLE_LIFE_SIGN
/** @brief Writes a life-sign context record for the application's name. */
extern void reportAppNameLifeSign(const char* appName);

/** @brief Writes a life-sign context record for the current thread's name. */
extern void reportCurrentThreadNameLifeSign(elog_thread_id_t threadId, const char* threadName);
#endif

}  // namespace elog

#endif  // __ELOG_INTERNAL_H__