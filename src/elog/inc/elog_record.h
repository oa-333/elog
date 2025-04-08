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

    /** @brief Default constructor. */
    ELogRecord()
        : m_logRecordId(0),
          m_logTime({0, 0}),
          m_threadId(0),
          m_sourceId(0),
          m_logLevel(ELEVEL_INFO),
          m_logMsg(nullptr) {}

    /** @brief Default copy constructor. */
    ELogRecord(const ELogRecord&) = default;

    /** @brief Destructor. */

    ~ELogRecord() {}

    inline ELogRecord& operator=(const ELogRecord& logRecord) = default;

    /*inline ELogRecord& operator=(const ELogRecord& logRecord) {
        m_logRecordId = logRecord.m_logRecordId;
        m_logTime = logRecord.m_logTime;
        m_threadId = logRecord.m_threadId;
        m_sourceId = logRecord.m_sourceId;
        m_logLevel = logRecord.m_logLevel;
        m_logMsg = (char*)logRecord.m_logMsg;
        return *this;
    }*/
};

}  // namespace elog

#endif  // __ELOG_RECORD_H__