#include "elog_field_selector.h"

#include "elog_def.h"

#ifdef ELOG_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX 256
#endif
#ifndef LOGIN_NAME_MAX
#define LOGIN_NAME_MAX 256
#endif
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#define PROG_NAME_MAX 256
#define THREAD_NAME_MAX 256

#include <climits>
#include <cstring>
#include <format>
#include <iomanip>
#include <unordered_map>

#include "elog_error.h"
#include "elog_system.h"

namespace elog {

static char hostName[HOST_NAME_MAX];
static char userName[LOGIN_NAME_MAX];
static char progName[PROG_NAME_MAX];
static thread_local char sThreadName[THREAD_NAME_MAX] = {};

#ifdef ELOG_ENABLE_STACK_TRACE
typedef std::unordered_map<dbgutil::os_thread_id_t, std::string> ThreadNameMap;
static ThreadNameMap sThreadNameMap;
static std::mutex sLock;

static void addThreadNameField(const char* threadName) {
    std::unique_lock<std::mutex> lock(sLock);
    sThreadNameMap.insert(ThreadNameMap::value_type(dbgutil::getCurrentThreadId(), threadName));
}

const char* getThreadNameField(dbgutil::os_thread_id_t threadId) {
    std::unique_lock<std::mutex> lock(sLock);
    ThreadNameMap::iterator itr = sThreadNameMap.find(threadId);
    if (itr == sThreadNameMap.end()) {
        return "";
    }
    return itr->second.c_str();
}
#endif

#ifdef ELOG_WINDOWS
static DWORD pid = 0;
#else
static pid_t pid = 0;
#endif

ELOG_IMPLEMENT_FIELD_SELECTOR(ELogRecordIdSelector)
ELOG_IMPLEMENT_FIELD_SELECTOR(ELogTimeSelector)
ELOG_IMPLEMENT_FIELD_SELECTOR(ELogHostNameSelector)
ELOG_IMPLEMENT_FIELD_SELECTOR(ELogUserNameSelector)
ELOG_IMPLEMENT_FIELD_SELECTOR(ELogProgramNameSelector)
ELOG_IMPLEMENT_FIELD_SELECTOR(ELogProcessIdSelector)
ELOG_IMPLEMENT_FIELD_SELECTOR(ELogThreadIdSelector)
ELOG_IMPLEMENT_FIELD_SELECTOR(ELogThreadNameSelector)
ELOG_IMPLEMENT_FIELD_SELECTOR(ELogSourceSelector)
ELOG_IMPLEMENT_FIELD_SELECTOR(ELogModuleSelector)
ELOG_IMPLEMENT_FIELD_SELECTOR(ELogFileSelector)
ELOG_IMPLEMENT_FIELD_SELECTOR(ELogLineSelector)
ELOG_IMPLEMENT_FIELD_SELECTOR(ELogFunctionSelector)
ELOG_IMPLEMENT_FIELD_SELECTOR(ELogLevelSelector)
ELOG_IMPLEMENT_FIELD_SELECTOR(ELogMsgSelector)

#define ELOG_MAX_FIELD_SELECTOR_COUNT 100

struct ELogFieldSelectorNameConstructor {
    const char* m_name;
    ELogFieldSelectorConstructor* m_ctor;
};

static ELogFieldSelectorNameConstructor sFieldConstructors[ELOG_MAX_FIELD_SELECTOR_COUNT] = {};
static uint32_t sFieldConstructorsCount = 0;

typedef std::unordered_map<std::string, ELogFieldSelectorConstructor*>
    ELogFieldSelectorConstructorMap;

static ELogFieldSelectorConstructorMap sFieldSelectorConstructorMap;

void registerFieldSelectorConstructor(const char* name, ELogFieldSelectorConstructor* constructor) {
    // due to c runtime issues we delay access to unordered map
    if (sFieldConstructorsCount >= ELOG_MAX_FIELD_SELECTOR_COUNT) {
        ELOG_REPORT_ERROR("Cannot register field selector constructor, no space: %s", name);
        exit(1);
    } else {
        // let order of registration decide dynamic type id
        // this is required since we need to support externally installed field selectors
        constructor->setTypeId(sFieldConstructorsCount);
        sFieldConstructors[sFieldConstructorsCount++] = {name, constructor};
    }
}

static bool applyFieldSelectorConstructorRegistration() {
    for (uint32_t i = 0; i < sFieldConstructorsCount; ++i) {
        ELogFieldSelectorNameConstructor& nameCtorPair = sFieldConstructors[i];
        if (!sFieldSelectorConstructorMap
                 .insert(ELogFieldSelectorConstructorMap::value_type(nameCtorPair.m_name,
                                                                     nameCtorPair.m_ctor))
                 .second) {
            ELOG_REPORT_ERROR("Duplicate field selector identifier: %s", nameCtorPair.m_name);
            return false;
        }
    }
    return true;
}

ELogFieldSelector* constructFieldSelector(const ELogFieldSpec& fieldSpec) {
    ELogFieldSelectorConstructorMap::iterator itr =
        sFieldSelectorConstructorMap.find(fieldSpec.m_name);
    if (itr == sFieldSelectorConstructorMap.end()) {
        ELOG_REPORT_ERROR("Invalid field selector %s: not found", fieldSpec.m_name.c_str());
        return nullptr;
    }

    ELogFieldSelectorConstructor* constructor = itr->second;
    ELogFieldSelector* fieldSelector = constructor->constructFieldSelector(fieldSpec);
    if (fieldSelector == nullptr) {
        ELOG_REPORT_ERROR("Failed to create field selector, out of memory");
    }
    return fieldSelector;
}

static void getProgName() {
    // set some default in case of error
    strcpy(progName, "N/A");

    // get executable file name
#ifdef ELOG_WINDOWS
    DWORD pathLen = GetModuleFileNameA(NULL, progName, PROG_NAME_MAX);
    if (pathLen == 0) {
        ELOG_REPORT_ERROR("WARNING: Failed to get executable file name: %u", GetLastError());
        return;
    }
    const char slash = '\\';
#else
    // can get it only from /proc/self/cmdline
    int fd = open("/proc/self/cmdline", O_RDONLY, 0);
    if (fd == -1) {
        ELOG_REPORT_ERROR("Failed to open /proc/self/cmdline for reading: %d", errno);
        return;
    }

    // read as many bytes as possible, the program name ends with a null (then program arguments
    // follow, but we don't care about them)
    ssize_t res = read(fd, progName, PROG_NAME_MAX);
    if (res == ((ssize_t)-1)) {
        ELOG_REPORT_ERROR("Failed to read from /proc/self/cmdline for reading: %d", errno);
        close(fd);
        return;
    }
    close(fd);

    // search for null and if not found then set one
    bool nullFound = false;
    for (uint32_t i = 0; i < PROG_NAME_MAX; ++i) {
        if (progName[i] == 0) {
            nullFound = true;
            break;
        }
    }
    if (!nullFound) {
        progName[PROG_NAME_MAX - 1] = 0;
    }
    const char slash = '/';
#endif

    // platform-agnostic part:
    // now search for last slash and pull string back
    char* slashPos = strrchr(progName, slash);
    if (slashPos != nullptr) {
        memmove(progName, slashPos + 1, strlen(slashPos + 1) + 1);
    }

    // finally remove extension
    char* dotPtr = strrchr(progName, '.');
    if (dotPtr != nullptr) {
        uint32_t dotPos = dotPtr - progName;
        progName[dotPos] = 0;
    }
}

extern bool initFieldSelectors() {
    if (!applyFieldSelectorConstructorRegistration()) {
        return false;
    }

// initialize host name
#ifdef ELOG_WINDOWS
    DWORD len = HOST_NAME_MAX;
    if (!GetComputerNameA(hostName, &len)) {
        strcpy(hostName, "<N/A>");
    }
#else
    if (gethostname(hostName, HOST_NAME_MAX) != 0) {
        strcpy(hostName, "<N/A>");
    }
#endif

    // initialize user name
#ifdef ELOG_WINDOWS
    len = LOGIN_NAME_MAX;
    if (!GetUserNameA(userName, &len)) {
        strcpy(userName, "<N/A>");
    }
#else  // ELOG_WINDOWS
    if (getlogin_r(userName, LOGIN_NAME_MAX) != 0) {
        strcpy(userName, "<N/A>");
    }
#endif

    // initialize program name
    getProgName();

    // initialize pid
#ifdef ELOG_WINDOWS
    pid = GetCurrentProcessId();
#else
    pid = getpid();
#endif  // ELOG_WINDOWS
    return true;
}

void termFieldSelectors() { sFieldSelectorConstructorMap.clear(); }

const char* getHostName() { return hostName; }

const char* getUserName() { return userName; }

const char* getProgramName() { return progName; }

void setCurrentThreadNameField(const char* threadName) {
    strncpy(sThreadName, threadName, THREAD_NAME_MAX);
#ifdef ELOG_ENABLE_STACK_TRACE
    addThreadNameField(threadName);
#endif
}

const char* getCurrentThreadNameField() { return sThreadName; }

void ELogStaticTextSelector::selectField(const ELogRecord& record, ELogFieldReceptor* receptor) {
    if (receptor->getFieldReceiveStyle() == ELogFieldReceptor::ReceiveStyle::RS_BY_NAME) {
        receptor->receiveStaticText(ELOG_INVALID_FIELD_SELECTOR_TYPE_ID, m_text, m_fieldSpec);
    } else {
        receptor->receiveStringField(ELOG_INVALID_FIELD_SELECTOR_TYPE_ID, m_text.c_str(),
                                     m_fieldSpec, m_text.length());
    }
}

void ELogRecordIdSelector::selectField(const ELogRecord& record, ELogFieldReceptor* receptor) {
    if (receptor->getFieldReceiveStyle() == ELogFieldReceptor::ReceiveStyle::RS_BY_NAME) {
        receptor->receiveRecordId(getTypeId(), record.m_logRecordId, m_fieldSpec);
    } else {
        receptor->receiveIntField(getTypeId(), record.m_logRecordId, m_fieldSpec);
    }
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

inline uint64_t formatInt(char* buf, uint64_t num, uint64_t width) {
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
static uint64_t unixELogFormatTime(char* buf, struct tm* tm_info) {
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
    buf[pos] = 0;
    return pos;
}
#endif

void ELogTimeSelector::selectField(const ELogRecord& record, ELogFieldReceptor* receptor) {
#ifdef ELOG_TIME_USE_CHRONO
    auto timePoint = std::chrono::time_point_cast<std::chrono::milliseconds>(record.m_logTime);
    std::chrono::zoned_time<std::chrono::milliseconds> zt(std::chrono::current_zone(), timePoint);
    std::string timeStr = std::format("{:%Y-%m-%d %H:%M:%S}", zt.get_local_time());
    receptor->receiveTimeField(getTypeId(), record.m_logTime, timeStr.c_str(), m_fieldSpec);
#else
    // this requires exactly 23 chars
    const size_t BUF_SIZE = 64;
    char timeStr[BUF_SIZE];
#ifdef ELOG_MSVC
    FILETIME localFileTime;
    SYSTEMTIME sysTime;
    if (FileTimeToLocalFileTime(&record.m_logTime, &localFileTime) &&
        FileTimeToSystemTime(&localFileTime, &sysTime)) {
        // it appears that this snprintf is very costly, so we revert to internal implementation
        /*size_t offset = snprintf(timeStr, BUF_SIZE, "%u-%.2u-%.2u %.2u:%.2u:%.2u.%.3u",
                                 sysTime.wYear, sysTime.wMonth, sysTime.wDay, sysTime.wHour,
                                 sysTime.wMinute, sysTime.wSecond, sysTime.wMilliseconds);*/
        size_t offset = win32ELogFormatTime(timeStr, &sysTime);
        receptor->receiveTimeField(getTypeId(), record.m_logTime, timeStr, m_fieldSpec);
    }
#else
    time_t timer = record.m_logTime.tv_sec;
    struct tm* tm_info = localtime(&timer);
    size_t offset = strftime(timeStr, 64, "%Y-%m-%d %H:%M:%S.", tm_info);
    // size_t offset = unixELogFormatTime(timeStr, tm_info);
    offset += snprintf(timeStr + offset, BUF_SIZE - offset, "%.3u",
                       (unsigned)(record.m_logTime.tv_nsec / 1000000L));
    receptor->receiveTimeField(getTypeId(), record.m_logTime, timeStr, m_fieldSpec);
#endif
#endif
}

void ELogHostNameSelector::selectField(const ELogRecord& record, ELogFieldReceptor* receptor) {
    if (receptor->getFieldReceiveStyle() == ELogFieldReceptor::ReceiveStyle::RS_BY_NAME) {
        receptor->receiveHostName(getTypeId(), hostName, m_fieldSpec);
    } else {
        receptor->receiveStringField(getTypeId(), hostName, m_fieldSpec);
    }
}

void ELogUserNameSelector::selectField(const ELogRecord& record, ELogFieldReceptor* receptor) {
    if (receptor->getFieldReceiveStyle() == ELogFieldReceptor::ReceiveStyle::RS_BY_NAME) {
        receptor->receiveUserName(getTypeId(), userName, m_fieldSpec);
    } else {
        receptor->receiveStringField(getTypeId(), userName, m_fieldSpec);
    }
}

void ELogProgramNameSelector::selectField(const ELogRecord& record, ELogFieldReceptor* receptor) {
    if (receptor->getFieldReceiveStyle() == ELogFieldReceptor::ReceiveStyle::RS_BY_NAME) {
        receptor->receiveProgramName(getTypeId(), progName, m_fieldSpec);
    } else {
        receptor->receiveStringField(getTypeId(), progName, m_fieldSpec);
    }
}

void ELogProcessIdSelector::selectField(const ELogRecord& record, ELogFieldReceptor* receptor) {
    if (receptor->getFieldReceiveStyle() == ELogFieldReceptor::ReceiveStyle::RS_BY_NAME) {
        receptor->receiveProcessId(getTypeId(), pid, m_fieldSpec);
    } else {
        receptor->receiveIntField(getTypeId(), pid, m_fieldSpec);
    }
}

void ELogThreadIdSelector::selectField(const ELogRecord& record, ELogFieldReceptor* receptor) {
    if (receptor->getFieldReceiveStyle() == ELogFieldReceptor::ReceiveStyle::RS_BY_NAME) {
        receptor->receiveThreadId(getTypeId(), record.m_threadId, m_fieldSpec);
    } else {
        receptor->receiveIntField(getTypeId(), record.m_threadId, m_fieldSpec);
    }
}

void ELogThreadNameSelector::selectField(const ELogRecord& record, ELogFieldReceptor* receptor) {
    if (receptor->getFieldReceiveStyle() == ELogFieldReceptor::ReceiveStyle::RS_BY_NAME) {
        receptor->receiveThreadName(getTypeId(), sThreadName, m_fieldSpec);
    } else {
        receptor->receiveStringField(getTypeId(), sThreadName, m_fieldSpec);
    }
}

void ELogSourceSelector::selectField(const ELogRecord& record, ELogFieldReceptor* receptor) {
    size_t logSourceNameLength = 0;
    const char* logSourceName = getLogSourceName(record, logSourceNameLength);
    if (receptor->getFieldReceiveStyle() == ELogFieldReceptor::ReceiveStyle::RS_BY_NAME) {
        receptor->receiveLogSourceName(getTypeId(), logSourceName, m_fieldSpec);
    } else {
        receptor->receiveStringField(getTypeId(), logSourceName, m_fieldSpec, logSourceNameLength);
    }
}

void ELogModuleSelector::selectField(const ELogRecord& record, ELogFieldReceptor* receptor) {
    size_t moduleNameLength = 0;
    const char* moduleName = getLogModuleName(record, moduleNameLength);
    if (receptor->getFieldReceiveStyle() == ELogFieldReceptor::ReceiveStyle::RS_BY_NAME) {
        receptor->receiveModuleName(getTypeId(), moduleName, m_fieldSpec);
    } else {
        receptor->receiveStringField(getTypeId(), moduleName, m_fieldSpec, moduleNameLength);
    }
}

void ELogFileSelector::selectField(const ELogRecord& record, ELogFieldReceptor* receptor) {
    if (receptor->getFieldReceiveStyle() == ELogFieldReceptor::ReceiveStyle::RS_BY_NAME) {
        receptor->receiveFileName(getTypeId(), record.m_file, m_fieldSpec);
    } else {
        receptor->receiveStringField(getTypeId(), record.m_file, m_fieldSpec);
    }
}

void ELogLineSelector::selectField(const ELogRecord& record, ELogFieldReceptor* receptor) {
    if (receptor->getFieldReceiveStyle() == ELogFieldReceptor::ReceiveStyle::RS_BY_NAME) {
        receptor->receiveLineNumber(getTypeId(), record.m_line, m_fieldSpec);
    } else {
        receptor->receiveIntField(getTypeId(), record.m_line, m_fieldSpec);
    }
}

void ELogFunctionSelector::selectField(const ELogRecord& record, ELogFieldReceptor* receptor) {
    if (receptor->getFieldReceiveStyle() == ELogFieldReceptor::ReceiveStyle::RS_BY_NAME) {
        receptor->receiveFunctionName(getTypeId(), record.m_function, m_fieldSpec);
    } else {
        receptor->receiveStringField(getTypeId(), record.m_function, m_fieldSpec);
    }
}

void ELogLevelSelector::selectField(const ELogRecord& record, ELogFieldReceptor* receptor) {
    receptor->receiveLogLevelField(getTypeId(), record.m_logLevel, m_fieldSpec);
}

void ELogMsgSelector::selectField(const ELogRecord& record, ELogFieldReceptor* receptor) {
    if (receptor->getFieldReceiveStyle() == ELogFieldReceptor::ReceiveStyle::RS_BY_NAME) {
        receptor->receiveLogMsg(getTypeId(), record.m_logMsg, m_fieldSpec);
    } else {
        receptor->receiveStringField(getTypeId(), record.m_logMsg, m_fieldSpec, record.m_logMsgLen);
    }
}

}  // namespace elog
