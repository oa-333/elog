#include "elog_time.h"

#include <charconv>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <new>

#include "elog_error.h"

#ifdef ELOG_TIME_USE_CHRONO
#include <sstream>
#endif

namespace elog {

// the maximum value of precomputed integer as string
#define ELOG_MAX_DATE_INT 3000

// maximum size of prepared integer as string (up to value 3000, including terminating null)
#define ELOG_INT_BUF_SIZE 5

/** @brief Integer represented as string, stuffed into 8 bytes struct */
struct IntStr {
    char m_buf[ELOG_INT_BUF_SIZE];
    uint16_t m_len;
    uint8_t m_padding;
};
static IntStr* sDateTable = nullptr;

// initialize the date table
bool initDateTable() {
    // TODO: have this aligned to cache line or 8 bytes at least
    sDateTable = new (std::nothrow) IntStr[ELOG_MAX_DATE_INT];
    if (sDateTable == nullptr) {
        ELOG_REPORT_ERROR("Failed to allocate int-string table fo size %u", ELOG_MAX_DATE_INT);
        return false;
    }
    for (uint32_t i = 0; i < ELOG_MAX_DATE_INT; ++i) {
        memset(&sDateTable[i], 0, sizeof(IntStr));
        std::to_chars(sDateTable[i].m_buf, sDateTable[i].m_buf + ELOG_INT_BUF_SIZE, i);
        sDateTable[i].m_len = strlen(sDateTable[i].m_buf);
    }
    return true;
}

// destroy the date table
void termDateTable() {
    if (sDateTable != nullptr) {
        delete[] sDateTable;
        sDateTable = nullptr;
    }
}

#ifdef ELOG_TIME_USE_CHRONO
static bool elogTimeFromStringChrono(const char* timeStr, ELogTime& logTime) {
    std::istringstream iss(timeStr);
#if __cpp_lib_chrono >= 201907L
    iss >> std::chrono::parse("%Y-%m-%d %H:%M:%S", logTime);
    return !iss.fail();
#else
    std::tm tm = {};
    iss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    if (iss.fail()) {
        return false
    }
    logTime = std::chrono::system_clock::from_time_t(std::mktime(&tm));
    return true;
#endif
}
#endif

#if !defined(ELOG_TIME_USE_CHRONO) && defined(ELOG_MSVC)
static bool elogSystemTimeFromStringWindows(const char* timeStr, SYSTEMTIME& sysTime) {
    const int DATE_TIME_ITEM_COUNT = 6;
    int ret = sscanf(timeStr, "%hu-%hu-%hu %hu:%hu:%hu", &sysTime.wYear, &sysTime.wMonth,
                     &sysTime.wDay, &sysTime.wHour, &sysTime.wMinute, &sysTime.wSecond);
    if (ret == EOF) {
        ELOG_REPORT_ERROR("Invalid time specification: %s", timeStr);
        return false;
    } else if (ret != DATE_TIME_ITEM_COUNT) {
        ELOG_REPORT_ERROR("Invalid time specification, missing date-time items: %s", timeStr, ret);
        return false;
    }
    return true;
}
#ifdef ELOG_TIME_USE_SYSTEMTIME
inline bool elogTimeFromStringWindows(const char* timeStr, ELogTime& logTime) {
    return elogSystemTimeFromStringWindows(timeStr, logTime);
}
#else
static bool elogTimeFromStringWindows(const char* timeStr, ELogTime& logTime) {
    SYSTEMTIME sysTime;
    if (!elogSystemTimeFromStringWindows(timeStr, sysTime)) {
        return false;
    }
    if (!SystemTimeToFileTime(&sysTime, &logTime)) {
        ELOG_REPORT_WIN32_ERROR(SystemTimeToFileTime,
                                "Failed to convert system time to file time: %s", timeStr);
        return false;
    }
    return true;
}
#endif
#endif

#if !defined(ELOG_TIME_USE_CHRONO) && !defined(ELOG_MSVC)
static bool elogTimeFromStringUnix(const char* timeStr, ELogTime& logTime) {
    struct tm tm;
    memset(&tm, 0, sizeof(struct tm));
#ifdef ELOG_MINGW
    const int DATE_TIME_ITEM_COUNT = 6;
    int ret = sscanf(timeStr, "%d-%d-%d %d:%d:%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
                     &tm.tm_hour, &tm.tm_min, &tm.tm_sec);
    if (ret == EOF) {
        ELOG_REPORT_ERROR("Invalid time specification: %s", timeStr);
        return false;
    } else if (ret != DATE_TIME_ITEM_COUNT) {
        ELOG_REPORT_ERROR("Invalid time specification, missing date-time items: %s", timeStr, ret);
        return false;
    }
    tm.tm_year -= 1900;  // adjust to number of years since 1900
    tm.tm_mon -= 1;      // adjust to range [0,11]
    // NOTE: tm_yday, tm_wday and tm_isdst are note set, is that ok?
#else
    char* ret = strptime(timeStr, "%Y-%m-%d %H:%M:%S", &tm);
    if (ret == nullptr) {
        ELOG_REPORT_ERROR("Invalid time specification: %s", timeStr);
        return false;
    } else if (*ret != 0) {
        ELOG_REPORT_ERROR("Invalid time specification, excess chars: %s (starting at %s)", timeStr,
                          ret);
        return false;
    }
#endif
    time_t tmVal = std::mktime(&tm);
    logTime.m_seconds = tmVal;
    logTime.m_100nanos = 0;
    return true;
}
#endif

bool elogTimeFromString(const char* timeStr, ELogTime& logTime) {
#ifdef ELOG_TIME_USE_CHRONO
    return elogTimeFromStringChrono(timeStr, logTime);
#else  // !defined ELOG_TIME_USE_CHRONO
#ifdef ELOG_MSVC
    return elogTimeFromStringWindows(timeStr, logTime);
#else   // !define ELOG_MSVC
    return elogTimeFromStringUnix(timeStr, logTime);
#endif  // ELOG_MSVC
#endif  // ELOG_TIME_USE_CHRONO
}

inline uint64_t getDigitCount(uint64_t num) {
    // the numbers we work with are small, so we should avoid using division operator
    if (num == 0) {
        return 0;
    } else if (num < 10) {
        return 1;
    } else if (num < 100) {
        return 2;
    } else if (num < 1000) {
        return 3;
    } else if (num < 10000) {
        return 4;
    } else {
        uint64_t digits = 0;
        while (num > 0) {
            ++digits;
            num /= 10;
        }
        return digits;
    }
}

inline uint64_t formatIntCalc(char* buf, uint64_t num, uint64_t width) {
    const char fill = '0';
    uint64_t pos = 0;
    uint64_t digits = getDigitCount(num);
    if (digits < width) {
        uint64_t fillCount = width - digits;
        for (uint64_t i = 0; i < fillCount; ++i) {
            buf[pos++] = fill;
        }
    }

    while (num > 0) {
        buf[pos + digits - 1] = (num % 10) + '0';
        num /= 10;
        digits--;
    }
    return width;
}

inline uint64_t formatInt(char* buf, uint64_t num, uint64_t width) {
    if (num < ELOG_MAX_DATE_INT) {
        const IntStr& intStr = sDateTable[num];
        int pos = 0;
        if (intStr.m_len < width) {
            memset(buf, '0', width - intStr.m_len);
            pos += width - intStr.m_len;
        }
        // using memcpy() instead of strcpy()/strncpy() is probably faster
        memcpy(buf + pos, intStr.m_buf, intStr.m_len);  // no need to copy terminating null
        return width;
    } else {
        return formatIntCalc(buf, num, width);
    }
}

#ifdef ELOG_MSVC
static uint64_t win32ELogFormatTime(char* buf, SYSTEMTIME* sysTime) {
    // convert year to string
    uint64_t pos = formatInt(buf, sysTime->wYear, 4);
    buf[pos++] = '-';
    pos += formatInt(buf + pos, sysTime->wMonth, 2);
    buf[pos++] = '-';
    pos += formatInt(buf + pos, sysTime->wDay, 2);
    buf[pos++] = ' ';
    pos += formatInt(buf + pos, sysTime->wHour, 2);
    buf[pos++] = ':';
    pos += formatInt(buf + pos, sysTime->wMinute, 2);
    buf[pos++] = ':';
    pos += formatInt(buf + pos, sysTime->wSecond, 2);
    buf[pos++] = '.';
    pos += formatInt(buf + pos, sysTime->wMilliseconds, 3);
    buf[pos] = 0;
    return pos;
}
#else
static uint64_t unixELogFormatTime(char* buf, struct tm* tm_info, uint32_t msec) {
    // convert year to string
    uint64_t pos = formatInt(buf, tm_info->tm_year, 4);
    buf[pos++] = '-';
    pos += formatInt(buf + pos, tm_info->tm_mon, 2);
    buf[pos++] = '-';
    pos += formatInt(buf + pos, tm_info->tm_mday, 2);
    buf[pos++] = ' ';
    pos += formatInt(buf + pos, tm_info->tm_hour, 2);
    buf[pos++] = ':';
    pos += formatInt(buf + pos, tm_info->tm_min, 2);
    buf[pos++] = ':';
    pos += formatInt(buf + pos, tm_info->tm_sec, 2);
    buf[pos++] = '.';
    pos += formatInt(buf + pos, msec, 3);
    buf[pos] = 0;
    return pos;
}
#endif

size_t elogTimeToString(const ELogTime& logTime, ELogTimeBuffer& timeBuffer) {
#ifdef ELOG_TIME_USE_CHRONO
    auto timePoint = std::chrono::time_point_cast<std::chrono::milliseconds>(record.m_logTime);
    std::chrono::zoned_time<std::chrono::milliseconds> zt(std::chrono::current_zone(), timePoint);
    std::string timeStr = std::format("{:%Y-%m-%d %H:%M:%S}", zt.get_local_time());
    return elog_strncpy(timeBuffer.m_buffer, timeStr.c_str(), sizeof(timeBuffer.m_buffer));
#else
    // this requires exactly 23 chars
    const size_t BUF_SIZE = 64;
    alignas(64) char timeStr[BUF_SIZE];
#ifdef ELOG_MSVC
#ifdef ELOG_TIME_USE_SYSTEMTIME
    // it appears that this snprintf is very costly, so we revert to internal implementation
    /*std::size_t offset = snprintf(timeStr, BUF_SIZE, "%u-%.2u-%.2u %.2u:%.2u:%.2u.%.3u",
                                  sysTime.wYear, sysTime.wMonth, sysTime.wDay, sysTime.wHour,
                                  sysTime.wMinute, sysTime.wSecond, sysTime.wMilliseconds);*/
    return win32ELogFormatTime(timeBuffer.m_buffer, &record.m_logTime);
#else
    FILETIME localFileTime;
    SYSTEMTIME sysTime;
    if (FileTimeToLocalFileTime(&logTime, &localFileTime) &&
        FileTimeToSystemTime(&localFileTime, &sysTime)) {
        // it appears that this snprintf is very costly, so we revert to internal implementation
        /*size_t offset = snprintf(timeStr, BUF_SIZE, "%u-%.2u-%.2u %.2u:%.2u:%.2u.%.3u",
                                 sysTime.wYear, sysTime.wMonth, sysTime.wDay, sysTime.wHour,
                                 sysTime.wMinute, sysTime.wSecond, sysTime.wMilliseconds);*/
        return win32ELogFormatTime(timeBuffer.m_buffer, &sysTime);
    }
#endif
#else
    time_t timer = logTime.m_seconds;
    struct tm* tm_info = localtime(&timer);
    // size_t offset = strftime(timeStr, 64, "%Y-%m-%d %H:%M:%S.", tm_info);
    // offset += snprintf(timeStr + offset, BUF_SIZE - offset, "%.3u",
    //                    (unsigned)(record.m_logTime.tv_nsec / 1000000L));
    return unixELogFormatTime(timeStr, tm_info, (unsigned)(logTime.m_100nanos / 10000UL));
#endif
#endif
    return 0;
}

}  // namespace elog
