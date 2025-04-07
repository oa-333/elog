#ifndef __ELOG_RECORD_H__
#define __ELOG_RECORD_H__

#include <cstdint>
#include <ctime>

#include "elog_level.h"

namespace elog {

struct ELogRecord {
    /** @var Log record id. */
    uint64_t m_logRecordId;

    /** @var Log time. */
    struct timeval m_logTime;

    // NOTE: host name, user name and process id do not require a field

    /** @var Issuing thread id. */
    uint64_t m_threadId;

    /** @var Issuing source id. */
    uint32_t m_sourceId;

    /** @var Log level. */
    ELogLevel m_logLevel;

    /** @var Formatted log message. */
    const char* m_logMsg;
};

}  // namespace elog

#endif  // __ELOG_RECORD_H__