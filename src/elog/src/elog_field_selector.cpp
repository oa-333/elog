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
#include <iomanip>

#include "elog_system.h"

namespace elog {

static char hostName[HOST_NAME_MAX];
static char userName[LOGIN_NAME_MAX];
static char progName[PROG_NAME_MAX];
static thread_local char sThreadName[THREAD_NAME_MAX] = {};

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
        ELogSystem::reportError("Cannot register field selector constructor, no space: %s", name);
        exit(1);
    } else {
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
            ELogSystem::reportError("Duplicate field selector identifier: %s", nameCtorPair.m_name);
            return false;
        }
    }
    return true;
}

ELogFieldSelector* constructFieldSelector(const char* name, int justify) {
    ELogFieldSelectorConstructorMap::iterator itr = sFieldSelectorConstructorMap.find(name);
    if (itr == sFieldSelectorConstructorMap.end()) {
        ELogSystem::reportError("Invalid field selector %s: not found", name);
        return nullptr;
    }

    ELogFieldSelectorConstructor* constructor = itr->second;
    ELogFieldSelector* fieldSelector = constructor->constructFieldSelector(justify);
    if (fieldSelector == nullptr) {
        ELogSystem::reportError("Failed to create field selector, out of memory");
    }
    return fieldSelector;
}

static void getProgName() {
    // set some default in case of error
    strcpy(progName, "N/A");

    // get executable file name
#ifdef ELOG_WINDOWS
    char modulePath[PROG_NAME_MAX];
    DWORD pathLen = GetModuleFileNameA(NULL, progName, PROG_NAME_MAX);
    if (pathLen == 0) {
        ELogSystem::reportError("WARNING: Failed to get executable file name: %u", GetLastError());
        return;
    }
    const char slash = '\\';
#else
    // can get it only from /proc/self/cmdline
    int fd = open("/proc/self/cmdline", O_RDONLY, 0);
    if (fd == -1) {
        ELogSystem::reportError("Failed to open /proc/self/cmdline for reading: %d", errno);
        return;
    }

    // read as many bytes as possible, the program name ends with a null (then program arguments
    // follow, but we don't care about them)
    ssize_t res = read(fd, progName, PROG_NAME_MAX);
    if (res == ((ssize_t)-1)) {
        ELogSystem::reportError("Failed to read from /proc/self/cmdline for reading: %d", errno);
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
}

void ELogStaticTextSelector::selectField(const ELogRecord& record, ELogFieldReceptor* receptor) {
    receptor->receiveStringField(m_text, m_justify);
}

void ELogRecordIdSelector::selectField(const ELogRecord& record, ELogFieldReceptor* receptor) {
    receptor->receiveIntField(record.m_logRecordId, m_justify);
}

void ELogTimeSelector::selectField(const ELogRecord& record, ELogFieldReceptor* receptor) {
    const uint32_t bufSize = 64;
    char buffer[bufSize];
#ifdef ELOG_MSVC
    size_t offset = snprintf(buffer, bufSize, "%u-%.2u-%.2u %.2u:%.2u:%.2u.%.3u",
                             record.m_logTime.wYear, record.m_logTime.wMonth, record.m_logTime.wDay,
                             record.m_logTime.wHour, record.m_logTime.wMinute,
                             record.m_logTime.wSecond, record.m_logTime.wMilliseconds);
    receptor->receiveTimeField(record.m_logTime, buffer, m_justify);
#else
    time_t timer = record.m_logTime.tv_sec;
    struct tm* tm_info = localtime(&timer);
    size_t offset = strftime(buffer, 64, "%Y-%m-%d %H:%M:%S.", tm_info);
    offset += snprintf(buffer + offset, bufSize - offset, "%.3u",
                       (unsigned)(record.m_logTime.tv_usec / 1000));
    receptor->receiveTimeField(record.m_logTime, buffer, m_justify);
#endif
}

void ELogHostNameSelector::selectField(const ELogRecord& record, ELogFieldReceptor* receptor) {
    receptor->receiveStringField(hostName, m_justify);
}

void ELogUserNameSelector::selectField(const ELogRecord& record, ELogFieldReceptor* receptor) {
    receptor->receiveStringField(userName, m_justify);
}

void ELogProgramNameSelector::selectField(const ELogRecord& record, ELogFieldReceptor* receptor) {
    receptor->receiveStringField(progName, m_justify);
}

void ELogProcessIdSelector::selectField(const ELogRecord& record, ELogFieldReceptor* receptor) {
    receptor->receiveIntField(pid, m_justify);
}

void ELogThreadIdSelector::selectField(const ELogRecord& record, ELogFieldReceptor* receptor) {
    receptor->receiveIntField(record.m_threadId, m_justify);
}

void ELogThreadNameSelector::selectField(const ELogRecord& record, ELogFieldReceptor* receptor) {
    receptor->receiveStringField(sThreadName, m_justify);
}

void ELogSourceSelector::selectField(const ELogRecord& record, ELogFieldReceptor* receptor) {
    ELogSource* logSource = ELogSystem::getLogSource(record.m_sourceId);
    if (logSource != nullptr && (*logSource->getQualifiedName() != 0)) {
        receptor->receiveStringField(logSource->getQualifiedName(), m_justify);
    } else {
        receptor->receiveStringField("N/A", m_justify);
    }
}

void ELogModuleSelector::selectField(const ELogRecord& record, ELogFieldReceptor* receptor) {
    ELogSource* logSource = ELogSystem::getLogSource(record.m_sourceId);
    if (logSource != nullptr && (*logSource->getModuleName() != 0)) {
        receptor->receiveStringField(logSource->getModuleName(), m_justify);
    } else {
        receptor->receiveStringField("N/A", m_justify);
    }
}

void ELogFileSelector::selectField(const ELogRecord& record, ELogFieldReceptor* receptor) {
    receptor->receiveStringField(record.m_file, m_justify);
}

void ELogLineSelector::selectField(const ELogRecord& record, ELogFieldReceptor* receptor) {
    receptor->receiveIntField(record.m_line, m_justify);
}

void ELogFunctionSelector::selectField(const ELogRecord& record, ELogFieldReceptor* receptor) {
    receptor->receiveStringField(record.m_function, m_justify);
}

void ELogLevelSelector::selectField(const ELogRecord& record, ELogFieldReceptor* receptor) {
    receptor->receiveLogLevelField(record.m_logLevel, m_justify);
}

void ELogMsgSelector::selectField(const ELogRecord& record, ELogFieldReceptor* receptor) {
    receptor->receiveStringField(record.m_logMsg, m_justify);
}

}  // namespace elog
