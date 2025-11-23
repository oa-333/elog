#ifndef __ELOG_API_LOG_TARGET_H__
#define __ELOG_API_LOG_TARGET_H__

#include "elog_common_def.h"
#include "elog_record.h"

namespace elog {

// initialize log target API
extern bool initLogTargets();

// terminate log target API
extern void termLogTargets();

#ifdef ELOG_ENABLE_DYNAMIC_CONFIG
// start log target GC
extern void startLogTargetGC();

class ELOG_API ELogFormatter;
class ELOG_API ELogFilter;
class ELOG_API ELogFlushPolicy;

// retired log target members to the log target GC
extern void retireLogTargetFormatter(ELogFormatter* logFormatter);
extern void retireLogTargetFilter(ELogFilter* logFilter);
extern void retireLogTargetFlushPolicy(ELogFlushPolicy* flushPolicy);

class ELOG_API ELogGC;

// access log target GC and epoch
extern ELogGC* getLogTargetGC();
extern std::atomic<uint64_t>& getLogTargetEpoch();
#endif

// send a log record to all log targets
extern bool logMsgTarget(const ELogRecord& logRecord, ELogTargetAffinityMask logTargetAffinityMask);

}  // namespace elog

#endif  // __ELOG_API_LOG_TARGET_H__