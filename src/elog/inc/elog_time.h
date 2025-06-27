#ifndef __ELOG_TIME_H__
#define __ELOG_TIME_H__

#include <cstdint>

#include "elog_def.h"

#ifdef ELOG_TIME_USE_CHRONO
#include <chrono>
#else
#include <time.h>
#endif

namespace elog {

// performance tests show that std chrono does not perform well so we use native functions instead.
// in particular, on Windows, using FILETIME performs better than SYSTEMTIME.
// in addition, it is desired to squeeze a single log record into exactly one cache line. On Windows
// FILETIME takes 8 bytes, but on Unix/Linux/MinGW timespec takes 12-16 bytes. But, timespec
// provides nanoseconds precision, which is really not needed for log record time. Therefore,
// instead of using timespec, we convert it to an 8 bytes 100-nanos precision number (just like
// filetime, only that the point of reference is different).
// so:
// by default ELOG_TIME_USE_CHRONO is NOT defined on all platforms
// by default ELOG_TIME_USE_SYSTEMTIME is NOT defined on Windows/MSVC

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
struct ELOG_API ELogTime {
    uint32_t m_seconds;
    uint32_t m_100nanos;
};
// typedef timespec ELogTime;
#endif
#endif

/** @def The expected log time string form. */
#define ELOG_TIME_PATTERN_STR "YYYY-MM-DD HH:MM:SS.XXX"

/** @def The expected length of log time in string form. */
#define ELOG_TIME_STR_LEN sizeof(ELOG_TIME_PATTERN_STR)

/** @struct Aligned cache buffer with enough space to hold time in string form. */
struct ELOG_API ELOG_CACHE_ALIGN ELogTimeBuffer {
    /**
     * @brief The time buffer. Although only 24 characters are needed (including terminating null),
     * we prefer to occupy entire cache line, so that after formatting time, it will not be removed
     * from cache due to false sharing.
     */
    char m_buffer[ELOG_CACHE_LINE];
};

/** @brief Retrieves the current time. */
inline void getCurrentTime(ELogTime& logTime) {
#ifdef ELOG_TIME_USE_CHRONO
    logTime = std::chrono::system_clock::now();
#else
#ifdef ELOG_MSVC
#ifdef ELOG_TIME_USE_SYSTEMTIME
    GetLocalTime(&logTime);
#else
    GetSystemTimeAsFileTime(&logTime);
#endif
#else
    // NOTE: gettimeofday is obsolete, instead clock_gettime() should be used
    timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    logTime.m_seconds = ts.tv_sec;
    logTime.m_100nanos = (uint32_t)(ts.tv_nsec / 100);
#endif
#endif
}

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
    uint64_t utcTimeNanos = logTime.m_seconds * 1000000000ULL + logTime.m_100nanos * 100;
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
 * @brief Converts log time to string.
 * @param logTime The log time.
 * @param buffer The output buffer, receiving the log time string.
 * @param length The output buffer length.
 * @return The number of formatted characters (not including the terminating null). This should
 * normally be the constant value ELOG_TIME_STR_LEN.
 */
extern ELOG_API size_t elogTimeToString(const ELogTime& logTime, ELogTimeBuffer& timeBuffer);

}  // namespace elog

#endif  // __ELOG_TIME_H__