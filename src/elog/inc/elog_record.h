#ifndef __ELOG_RECORD_H__
#define __ELOG_RECORD_H__

#include <cstdint>
#include <cstring>

#ifdef ELOG_TIME_USE_CHRONO
#include <chrono>
#else
#include <time.h>
#endif

#include "elog_level.h"

namespace elog {

// performance tests show that std chrono does not perform well so we use native functions instead
// particularly, on Windows, using FILETIME performs better than SYSTEMTIME
// so:
// by default ELOG_TIME_USE_CHRONO is NOT defined
// by default on Windows/MSVC, SYSTEMTIME is NOT defined

#ifdef ELOG_TIME_USE_CHRONO
typedef std::chrono::system_clock::time_point ELogTime;
#else
#ifdef ELOG_MSVC
#ifdef ELOG_TIME_USE_SYSTEMTIME
typedef SYSTEMTIME ELogTime;
#else
typedef FILETIME ELogTime;
#endif
#else
typedef timespec ELogTime;
#endif
#endif

// forward declaration
class ELOG_API ELogLogger;

struct ELOG_API ELogRecord {
    /** @var Log record id. */
    uint64_t m_logRecordId;

    /** @var Log time. */
    ELogTime m_logTime;

    // NOTE: host name, user name and process id do not require a field

    /** @var Issuing thread id. */
    uint64_t m_threadId;

    /** @var Issuing logger. */
    ELogLogger* m_logger;

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

    /** @var Formatted log message length (assist in buffer requirement estimation). */
    uint32_t m_logMsgLen;

    /** @var Reserved for internal use. */
    void* m_reserved;

    /** @brief Default constructor. */
    ELogRecord()
        : m_logRecordId(0),
#ifdef ELOG_TIME_USE_CHRONO
          m_logTime(std::chrono::system_clock::now()),
#endif
          m_threadId(0),
          m_logger(nullptr),
          m_logLevel(ELEVEL_INFO),
          m_logMsg(nullptr),
          m_logMsgLen(0),
          m_reserved(nullptr) {
    }

    /** @brief Default copy constructor. */
    ELogRecord(const ELogRecord&) = default;

    /** @brief Destructor. */
    ~ELogRecord() {}

    inline ELogRecord& operator=(const ELogRecord& logRecord) = default;

    inline bool operator==(const ELogRecord& logRecord) const {
        return m_logRecordId == logRecord.m_logRecordId;
    }
};

#ifdef ELOG_MSVC
#define UNIX_MSVC_DIFF_SECONDS 11644473600LL
#define SECONDS_TO_100NANOS(seconds) ((seconds) * 10000000LL)
#define FILE_TIME_TO_LL(ft) (*(LONGLONG*)&(ft))
#define FILETIME_TO_UNIXTIME_NANOS(ft) \
    (FILE_TIME_TO_LL(ft) - SECONDS_TO_100NANOS(UNIX_MSVC_DIFF_SECONDS)) * 100LL
#define FILETIME_TO_UNIXTIME(ft) FILETIME_TO_UNIXTIME_NANOS(ft) / 1000000000LL
#define UNIXTIME_TO_FILETIME(ut, ft) \
    FILE_TIME_TO_LL(ft) = SECONDS_TO_100NANOS(ut + UNIX_MSVC_DIFF_SECONDS)
// #define FILETIME_TO_UNIXTIME(ft) ((*(LONGLONG*)&(ft) - 116444736000000000LL) / 10000000LL)
// #define UNIXTIME_TO_FILETIME(ut, ft) (*(LONGLONG*)&(ft) = (ut) * 10000000LL +
// 116444736000000000LL)
#endif

inline uint64_t elogTimeToUTCNanos(const ELogTime& logTime) {
#ifdef ELOG_TIME_USE_CHRONO
    auto epochNanos =
        std::chrono::duration_cast<std::chrono::nanoseconds>(logTime.time_since_epoch());
    uint64_t utcTimeNanos = epochMillis.count();
    return utcTimeNanos;
#elif defined(ELOG_MSVC)
#ifdef ELOG_TIME_USE_SYSTEMTIME
    FILETIME ft = {};
    if (SystemTimeToFileTime(&logTime, &ft)) {
        uint64_t utcTimeNanos = (uint64_t)FILETIME_TO_UNIXTIME_NANOS(ft);
        return utcTimeNanos;
    }
    return 0;
#else
    uint64_t utcTimeNanos = (uint64_t)FILETIME_TO_UNIXTIME_NANOS(logTime);
    return utcTimeNanos;
#endif
#else
    uint64_t utcTimeNanos = logTime.tv_sec * 1000000000ULL + logTime.tv_nsec;
    return utcTimeNanos;
#endif
}

inline uint64_t elogTimeToUTCSeconds(const ELogTime& logTime) {
    return elogTimeToUTCNanos(logTime) / 1000000000ULL;
}

/**
 * @brief Converts time string to ELog time type.
 * @param timeStr The input time string, expected in the format "YYYY-mm-dd HH::MM::SS"
 * @param[out] logTime The resulting log time.
 * @return True if conversion was successful, otherwise false.
 */
extern ELOG_API bool elogTimeFromString(const char* timeStr, ELogTime& logTime);

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