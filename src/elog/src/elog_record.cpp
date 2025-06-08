#include "elog_record.h"

#include <cstdio>
#include <ctime>

#include "elog_error.h"
#include "elog_logger.h"

#ifdef ELOG_TIME_USE_CHRONO
#include <sstream>
#endif

namespace elog {

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
static bool elogTimeFromStringWindows(const char* timeStr, ELogTime& logTime) {
    const int DATE_TIME_ITEM_COUNT = 6;
    SYSTEMTIME sysTime;
    int ret = sscanf(timeStr, "%hu-%hu-%hu %hu:%hu:%hu", &sysTime.wYear, &sysTime.wMonth,
                     &sysTime.wDay, &sysTime.wHour, &sysTime.wMinute, &sysTime.wSecond);
    if (ret == EOF) {
        ELOG_REPORT_ERROR("Invalid time specification: %s", timeStr);
        return false;
    } else if (ret != DATE_TIME_ITEM_COUNT) {
        ELOG_REPORT_ERROR("Invalid time specification, missing date-time items: %s", timeStr, ret);
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
    logTime.tv_sec = tmVal;
    logTime.tv_nsec = 0;
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

static const char ELOG_ROOT_NAME[] = "elog_root";
static const char ELOG_MODULE_NAME[] = "elog";

const char* getLogSourceName(const ELogRecord& logRecord, size_t& length) {
    const char* logSourceName = logRecord.m_logger->getLogSource()->getQualifiedName();
    length = logRecord.m_logger->getLogSource()->getQualifiedNameLength();
    if (logSourceName == nullptr || *logSourceName == 0) {
        logSourceName = ELOG_ROOT_NAME;
        length = sizeof(ELOG_ROOT_NAME) - 1;  // do not include terminating null
    }
    return logSourceName;
}

const char* getLogModuleName(const ELogRecord& logRecord, size_t& length) {
    const char* moduleName = logRecord.m_logger->getLogSource()->getModuleName();
    length = logRecord.m_logger->getLogSource()->getModuleNameLength();
    if (moduleName == nullptr || *moduleName == 0) {
        moduleName = ELOG_MODULE_NAME;
        length = sizeof(ELOG_MODULE_NAME) - 1;  // do not include terminating null
    }
    return moduleName;
}

}  // namespace elog
