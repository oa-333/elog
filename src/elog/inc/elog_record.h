#ifndef __ELOG_RECORD_H__
#define __ELOG_RECORD_H__

#include <cstdint>
#include <cstring>

#include "elog_level.h"
#include "elog_time.h"

namespace elog {

// forward declaration
class ELOG_API ELogLogger;

struct ELOG_API ELogRecord {
    /** @var Log record id (word offset: 0). */
    uint64_t m_logRecordId;

    /** @var Log time (word offset: 1). */
    ELogTime m_logTime;

    // NOTE: host name, user name and process id do not require a field

    /** @var Issuing thread id (word offset: 2). */
    uint32_t m_threadId;

    /** @var Log level. */
    ELogLevel m_logLevel;

    /** @var Issuing logger (word offset: 3). */
    ELogLogger* m_logger;

    /** @var Issuing file (word offset: 4). */
    const char* m_file;

    /** @var Issuing function (word offset: 5). */
    const char* m_function;

    /** @var Formatted log message (word offset: 6). */
    const char* m_logMsg;

    // this is the 7th word in the struct, exactly aligned to cache line

    /** @var Formatted log message length (assist in buffer requirement estimation). */
    uint32_t m_logMsgLen;

    /** @var Issuing line. */
    uint16_t m_line;

    /** @var Reserved for internal use. */
    uint16_t m_reserved;

    /** @brief Default constructor. */
    ELogRecord()
        : m_logRecordId(0),
#ifdef ELOG_TIME_USE_CHRONO
          m_logTime(std::chrono::system_clock::now()),
#endif
          m_threadId(0),
          m_logLevel(ELEVEL_INFO),
          m_logger(nullptr),
          m_file(nullptr),
          m_function(nullptr),
          m_logMsg(nullptr),
          m_logMsgLen(0),
          m_line(0),
          m_reserved(0) {
    }

    /** @brief Default copy constructor. */
    ELogRecord(const ELogRecord&) = default;

    /** @brief Default move constructor. */
    ELogRecord(ELogRecord&&) = delete;

    /** @brief Destructor. */
    ~ELogRecord() {}

    /** @brief Use default assignment operator. */
    inline ELogRecord& operator=(const ELogRecord& logRecord) = default;

    /** @brief Compare two log records by pair thread-id/record-id. */
    inline bool operator==(const ELogRecord& logRecord) const {
        return m_threadId == logRecord.m_threadId && m_logRecordId == logRecord.m_logRecordId;
    }
};

/**
 * @brief Get the log source name form the log record.
 * @param logRecord The input log record.
 * @param[out] length The resulting length of the log source name.
 * @return The log source name.
 */
extern ELOG_API const char* getLogSourceName(const ELogRecord& logRecord, size_t& length);

/**
 * @brief Get the log module name form the log record.
 * @param logRecord The input log record.
 * @param[out] length The resulting length of the log module name.
 * @return The log module name.
 */
extern ELOG_API const char* getLogModuleName(const ELogRecord& logRecord, size_t& length);

}  // namespace elog

#endif  // __ELOG_RECORD_H__