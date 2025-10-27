#ifndef __ELOG_API_LOG_TARGET_H__
#define __ELOG_API_LOG_TARGET_H__

#include "elog_common_def.h"
#include "elog_record.h"

namespace elog {

// initialize log target API
extern bool initLogTargets();

// terminate log target API
extern void termLogTargets();

// send a log record to all log targets
extern void logMsgTarget(const ELogRecord& logRecord, ELogTargetAffinityMask logTargetAffinityMask);

}  // namespace elog

#endif  // __ELOG_API_LOG_TARGET_H__