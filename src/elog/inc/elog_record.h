#ifndef __ELOG_RECORD_H__
#define __ELOG_RECORD_H__

#include <chrono>
#include <cstdint>
#include <ctime>

#include "elog_level.h"

namespace elog {

typedef std::chrono::system_clock::time_point ELogTime;

struct ELOG_API ELogRecord {
    /** @var Log record id. */
    uint64_t m_logRecordId;

    /** @var Log time. */
    ELogTime m_logTime;

    // NOTE: host name, user name and process id do not require a field

    /** @var Issuing thread id. */
    uint64_t m_threadId;

    /** @var Issuing source id. */
    uint32_t m_sourceId;

    /** @var Log level. */
    ELogLevel m_logLevel;

    /** @var Issuing file. */
    const char* m_file;

    /** @var Issuing line. */
    int m_line;

    /** @var Issuing function. */
    const char* m_function;

    /** @var Formatted log message. */
    const char* m_logMsg;

    /** @var Reserved for internal use. */
    void* m_reserved;

    /** @brief Default constructor. */
    ELogRecord()
        : m_logRecordId(0),
          m_logTime(std::chrono::system_clock::now()),
          m_threadId(0),
          m_sourceId(0),
          m_logLevel(ELEVEL_INFO),
          m_logMsg(nullptr),
          m_reserved(nullptr) {}

    /** @brief Default copy constructor. */
    ELogRecord(const ELogRecord&) = default;

    /** @brief Destructor. */
    ~ELogRecord() {}

    inline ELogRecord& operator=(const ELogRecord& logRecord) = default;

    inline bool operator==(const ELogRecord& logRecord) const {
        return m_logRecordId == logRecord.m_logRecordId;
    }
};

}  // namespace elog

#endif  // __ELOG_RECORD_H__