#ifndef __ELOG_TIME_H__
#define __ELOG_TIME_H__

#include <chrono>
#include <cstdint>

#include "elog_def.h"

#ifndef ELOG_TIME_USE_CHRONO
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
/** @brief A reference point so that we can squeeze time point into 32 bits. */
extern ELOG_API const time_t sUnixTimeRef;

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
inline void elogGetCurrentTime(ELogTime& logTime) {
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
    // in order to squeeze time to 32 bit we save it from year 2000 offset, this way we get
    timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    logTime.m_seconds = ts.tv_sec - sUnixTimeRef;
    logTime.m_100nanos = (uint32_t)(ts.tv_nsec / 100);
#endif
#endif
}

/**
 * @brief Checks whether to log time objects are equal
 */
inline bool elogTimeEquals(const ELogTime& lhs, const ELogTime& rhs) {
#ifdef ELOG_TIME_USE_CHRONO
    return lhs == rhs;
#else
#ifdef ELOG_MSVC
#ifdef ELOG_TIME_USE_SYSTEMTIME
    return lhs.wMilliseconds == rhs.wMilliseconds && lhs.wSecond == rhs.wSecond &&
           lhs.wMinute == rhs.wMinute && lhs.wHour == rhs.wHour && lhs.wDay == rhs.wDay &&
           lhs.wMonth == rhs.wMonth && lhs.wYear == rhs.wYear;
#else
    return lhs.dwLowDateTime == rhs.dwLowDateTime && lhs.dwHighDateTime == rhs.dwHighDateTime;
#endif
#else
    return lhs.m_seconds == rhs.m_seconds && lhs.m_100nanos == rhs.m_100nanos;
#endif
#endif
}

/**
 * @brief Converts ELog time to UNIX time nanoseconds (epoch since 1/1/1970 00:00:00 UTC).
 *
 * @note Although the UNIX time is defined in units of seconds, this API function allows to specify
 * nano-second accuracy. In reality, the ELog time stamp has accuracy of 100 nanoseconds.
 *
 * @param logTime The elog time.
 * @param useLocalTime Specifies whether local time should be used to make the conversion (some log
 * targets, such as Grafana may require this).
 * @return The UNIX time in nanoseconds.
 */
extern ELOG_API uint64_t elogTimeToUnixTimeNanos(const ELogTime& logTime,
                                                 bool useLocalTime = false);

/**
 * @brief Converts ELog time to UNIX time milliseconds (epoch since 1/1/1970 00:00:00 UTC).
 *
 * @param logTime The elog time.
 * @param useLocalTime Specifies whether local time should be used to make the conversion (some log
 * targets, such as Grafana may require this).
 * @return The UNIX time in seconds.
 */
inline uint64_t elogTimeToUnixTimeMilliseconds(const ELogTime& logTime, bool useLocalTime = false) {
    return elogTimeToUnixTimeNanos(logTime, useLocalTime) / 1000000ULL;
}

/**
 * @brief Converts ELog time to UNIX time seconds (epoch since 1/1/1970 00:00:00 UTC).
 *
 * @param logTime The elog time.
 * @param useLocalTime Specifies whether local time should be used to make the conversion (some log
 * targets, such as Grafana may require this).
 * @return The UNIX time in seconds.
 */
inline uint64_t elogTimeToUnixTimeSeconds(const ELogTime& logTime, bool useLocalTime = false) {
    return elogTimeToUnixTimeNanos(logTime, useLocalTime) / 1000000000ULL;
}

/**
 * @brief Converts an ELogTime to a 64 bit integer value. This is a bit more optimized than @ref
 * elogTimeToUnixTimeNanos().
 */
extern ELOG_API uint64_t elogTimeToInt64(const ELogTime& elogTime);

/** @brief Converts a 64 bit integer value to an ELogTime. */
extern ELOG_API void elogTimeFromInt64(uint64_t timeStamp, ELogTime& elogTime);

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
 * @param timeBuffer The output buffer, receiving the log time string.
 * @return The number of formatted characters (not including the terminating null). This should
 * normally be the constant value ELOG_TIME_STR_LEN.
 */
extern ELOG_API size_t elogTimeToString(const ELogTime& logTime, ELogTimeBuffer& timeBuffer);

/** @brief Retrieves a time stamp. */
// TODO: change this name, it is too much similar to elogGetCurrentTime()
inline uint64_t getCurrentTimeMillis() {
    return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

}  // namespace elog

#endif  // __ELOG_TIME_H__