#include "elog_field_selector.h"

#include "elog_def.h"
#include "elog_internal.h"

#ifdef ELOG_WINDOWS
#ifdef ELOG_MINGW
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif
#include <WinSock2.h>
#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX 256
#endif
#ifndef LOGIN_NAME_MAX
#define LOGIN_NAME_MAX 256
#endif
#else
#include <fcntl.h>
#include <sys/utsname.h>
#include <unistd.h>
#endif

#define OS_NAME_MAX 256
#define OS_VERSION_MAX 256
#define APP_NAME_MAX 256
#define PROG_NAME_MAX 256

// windows version hack
#ifdef ELOG_WINDOWS
// For more info regarding theses constants, refer to:
// https://www.codeproject.com/Articles/5336372/Windows-Version-Detection
#define WIN32_VERSION_BASE_ADDR 0x7FFE0000
#define WIN32_VERSION_MAJOR_OFFSET 0x26c
#define WIN32_VERSION_MINOR_OFFSET 0x270
#define WIN32_VERSION_BUILD_NUM_OFFSET 0x260
#endif

#include <cassert>
#include <climits>
#include <cstring>
#include <format>
#include <iomanip>
#include <mutex>
#include <regex>
#include <sstream>
#include <unordered_map>

#include "elog_common.h"
#include "elog_concurrent_hash_table.h"
#include "elog_field_selector_internal.h"
#include "elog_filter.h"
#include "elog_report.h"
#include "elog_tls.h"

#ifdef ELOG_ENABLE_FMT_LIB
#include "elog_logger.h"
#endif

#ifdef ELOG_ENABLE_LIFE_SIGN
#include "elog_internal.h"
#endif

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogFieldSelector)

typedef ELogConcurrentHashTable<const char*> ELogThreadNameMap;
static ELogThreadNameMap sThreadNameMap;
static ELogTlsKey sThreadNameKey = ELOG_INVALID_TLS_KEY;

// determines how sparse the thread hash map will be to achieve less collision
#define ELOG_THREAD_HASH_MAP_FACTOR 4

struct ELogThreadData {
    ELogThreadData(uint32_t threadId = 0) : m_threadId(threadId) {
#ifdef ELOG_ENABLE_LIFE_SIGN
        m_notifier = nullptr;
#endif
    }

    ELogThreadData(const ELogThreadData&) = default;
    ELogThreadData(ELogThreadData&&) = delete;
    ELogThreadData& operator=(const ELogThreadData&) = default;
    ~ELogThreadData() {}

    uint32_t m_threadId;
#ifdef ELOG_ENABLE_LIFE_SIGN
    dbgutil::ThreadNotifier* m_notifier;
#endif
};
typedef std::unordered_map<std::string, ELogThreadData> ELogThreadDataMap;
static ELogThreadDataMap sThreadDataMap;
static std::mutex sLock;

static char sHostName[HOST_NAME_MAX];
static char sUserName[LOGIN_NAME_MAX];
static char sOsName[OS_NAME_MAX];
static char sOsVersion[OS_VERSION_MAX];
static char sAppName[APP_NAME_MAX];
static char sProgName[PROG_NAME_MAX];
static thread_local char* sThreadName = nullptr;

static void cleanupThreadName(void* key) {
    // NOTE: self report handler cannot be used because the shared logger's thread local buffer may
    // be already destroyed (depending on TLS destruction order) so we force using the default
    // handler
    ELOG_SCOPED_DEFAULT_REPORT();

    // cleanup both maps and deallocate memory for thread name
    elog_thread_id_t threadId = (elog_thread_id_t)(uint64_t)key;
    const char* threadName = getThreadNameField(threadId);
    if (threadName == nullptr || *threadName == 0) {
        ELOG_REPORT_WARN("Cannot cleanup thread name for current thread, thread name is null");
        return;
    }
    ELOG_REPORT_TRACE("Cleaning up thread name %s", threadName);
    uint32_t entryId = sThreadNameMap.removeItem(threadId);
    ELOG_REPORT_TRACE("Removed thread name at entry %u", entryId);

    // cleanup inverse map as well
    std::unique_lock<std::mutex> lock(sLock);
    ELogThreadDataMap::iterator itr = sThreadDataMap.find(threadName);
    if (itr != sThreadDataMap.end()) {
        sThreadDataMap.erase(itr);
    }

    free((void*)threadName);
    sThreadName = nullptr;
}

#ifdef ELOG_WINDOWS
static DWORD pid = 0;
#else
static pid_t pid = 0;
#endif

ELOG_IMPLEMENT_FIELD_SELECTOR(ELogStaticTextSelector)
ELOG_IMPLEMENT_FIELD_SELECTOR(ELogRecordIdSelector)
ELOG_IMPLEMENT_FIELD_SELECTOR(ELogTimeSelector)
ELOG_IMPLEMENT_FIELD_SELECTOR(ELogTimeEpochSelector)
ELOG_IMPLEMENT_FIELD_SELECTOR(ELogHostNameSelector)
ELOG_IMPLEMENT_FIELD_SELECTOR(ELogUserNameSelector)
ELOG_IMPLEMENT_FIELD_SELECTOR(ELogOsNameSelector)
ELOG_IMPLEMENT_FIELD_SELECTOR(ELogOsVersionSelector)
ELOG_IMPLEMENT_FIELD_SELECTOR(ELogAppNameSelector)
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
ELOG_IMPLEMENT_FIELD_SELECTOR(ELogFormatSelector);
ELOG_IMPLEMENT_FIELD_SELECTOR(ELogIfSelector);
ELOG_IMPLEMENT_FIELD_SELECTOR(ELogSwitchSelector);
ELOG_IMPLEMENT_FIELD_SELECTOR(ELogExprSwitchSelector);
ELOG_IMPLEMENT_FIELD_SELECTOR(ELogConstStringSelector);
ELOG_IMPLEMENT_FIELD_SELECTOR(ELogConstIntSelector);
ELOG_IMPLEMENT_FIELD_SELECTOR(ELogConstTimeSelector);
ELOG_IMPLEMENT_FIELD_SELECTOR(ELogConstLogLevelSelector);

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

static void initHostName() {
#ifdef ELOG_WINDOWS
    DWORD len = HOST_NAME_MAX;
    if (!GetComputerNameA(sHostName, &len)) {
        elog_strncpy(sHostName, "<N/A>", HOST_NAME_MAX);
    }
#else
    if (gethostname(sHostName, HOST_NAME_MAX) != 0) {
        elog_strncpy(sHostName, "<N/A>", HOST_NAME_MAX);
    }
#endif
}

static void initUserName() {
#ifdef ELOG_WINDOWS
    DWORD len = LOGIN_NAME_MAX;
    if (!GetUserNameA(sUserName, &len)) {
        elog_strncpy(sUserName, "<N/A>", LOGIN_NAME_MAX);
    }
#else  // ELOG_WINDOWS
    if (getlogin_r(sUserName, LOGIN_NAME_MAX) != 0) {
        elog_strncpy(sUserName, "<N/A>", LOGIN_NAME_MAX);
    }
#endif
}

#ifdef ELOG_WINDOWS
static const char* getWin32OSName(LPOSVERSIONINFOEXA verInfo) {
    if (verInfo->dwMajorVersion == 10) {
        if (verInfo->wProductType == VER_NT_WORKSTATION) {
            return "Windows 10";
        } else {
            return "Windows Server 2016";
        }
    } else if (verInfo->dwMajorVersion == 6) {
        if (verInfo->wProductType == VER_NT_WORKSTATION) {
            if (verInfo->dwMinorVersion == 3) {
                return "Windows 8.1";
            } else if (verInfo->dwMinorVersion == 2) {
                return "Windows 8";
            } else if (verInfo->dwMinorVersion == 1) {
                return "Windows 7";
            } else if (verInfo->dwMinorVersion == 0) {
                return "Windows Vista";
            }
        } else {
            if (verInfo->dwMinorVersion == 3) {
                return "Windows Server 2012 R2";
            } else if (verInfo->dwMinorVersion == 2) {
                return "Windows Server 2012";
            } else if (verInfo->dwMinorVersion == 1) {
                return "Windows Server 2008 R2";
            } else if (verInfo->dwMinorVersion == 0) {
                return "Windows Server 2008";
            }
        }
    } else if (verInfo->dwMajorVersion == 5) {
        if (verInfo->wProductType == VER_NT_WORKSTATION) {
            if (verInfo->dwMinorVersion == 2) {
                // NOTE: we don't test for architecture as documentation requires since we support
                // only x64 builds for windows platforms
                return "Windows XP 64-Bit Edition";
            } else if (verInfo->dwMinorVersion == 1) {
                return "Windows XP";
            } else if (verInfo->dwMinorVersion == 0) {
                return "Windows 2000";
            }
        } else {
            if (verInfo->dwMinorVersion == 2) {
                if (verInfo->wSuiteMask & VER_SUITE_WH_SERVER) {
                    return "Windows Home Server";
                } else if (GetSystemMetrics(SM_SERVERR2) != 0) {
                    return "Windows Server 2003 R2";
                } else {
                    return "Windows Server 2003";
                }
            }
        }
    }
    return "";
}

/*static const char* getWin32SuiteName(LPOSVERSIONINFOEXA verInfo) {
    // currently we don't support this, is it really needed?
    return "";
}*/

static const char* getWin32ProductName(LPOSVERSIONINFOEXA verInfo) {
    if (verInfo->wProductType == VER_NT_DOMAIN_CONTROLLER) {
        return "Domain Controller";
    } else if (verInfo->wProductType == VER_NT_SERVER) {
        return "Server";
    } else if (verInfo->wProductType == VER_NT_WORKSTATION) {
        return "Workstation";
    } else {
        return "";
    }
}
#endif

#ifdef ELOG_LINUX
static bool getLinuxDistribution(std::string& dist) {
    const char* cmd = "lsb_release -d | awk {'first = $1; $1=\"\"; print $0'}|sed 's/^ //g'";
    FILE* fp = popen(cmd, "r");
    if (fp == NULL) {
        ELOG_REPORT_SYS_ERROR(popen, "Failed to run command: %s", cmd);
        return false;
    }
    char buf[1024];
    while (fgets(buf, sizeof(buf), fp) != NULL) {
        dist += buf;
    }
    pclose(fp);
    return true;
}
#endif

static void initOsNameAndVersion() {
#ifdef ELOG_WINDOWS
    // get version info
    OSVERSIONINFOEXA verInfo;
    ZeroMemory(&verInfo, sizeof(OSVERSIONINFOEXA));
    verInfo.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEXA);
    GetVersionExA((LPOSVERSIONINFOA)&verInfo);

    // TODO: This is a hack, since embedding manifest is very awkward and does not work...
    // (it is unclear whether the manifest can be embedded within any DLL or it must be embedded
    // within the main executable image)
    auto sharedUserData = (BYTE*)WIN32_VERSION_BASE_ADDR;
    auto majorVerPtr = (ULONG*)(sharedUserData + WIN32_VERSION_MAJOR_OFFSET);
    auto minorVerPtr = (ULONG*)(sharedUserData + WIN32_VERSION_MINOR_OFFSET);
    auto buildNumPtr = (ULONG*)(sharedUserData + WIN32_VERSION_BUILD_NUM_OFFSET);
    if (!IsBadReadPtr(majorVerPtr, sizeof(ULONG)) && !IsBadReadPtr(minorVerPtr, sizeof(ULONG)) &&
        !IsBadReadPtr(buildNumPtr, sizeof(ULONG))) {
        verInfo.dwMajorVersion = *majorVerPtr;
        verInfo.dwMinorVersion = *minorVerPtr;
        verInfo.dwBuildNumber = *buildNumPtr;
    }
    // Done hack stuff

    // format OS name
    std::stringstream s;
    s << getWin32OSName(&verInfo) << " " << getWin32ProductName(&verInfo);
#ifdef ELOG_MINGW
    s << " (MSYS2)";
#endif
    std::string osName = s.str();
    elog_strncpy(sOsName, osName.c_str(), OS_NAME_MAX);

    // format version
    s.str(std::string());  // clear string stream contents
    s << verInfo.dwMajorVersion << "." << verInfo.dwMinorVersion << "." << verInfo.dwBuildNumber;
    std::string osVer = s.str();
    elog_strncpy(sOsVersion, osVer.c_str(), OS_VERSION_MAX);
#else
    sOsName[0] = 0;
    sOsVersion[0] = 0;
    struct utsname buf = {};
    if (uname(&buf) == -1) {
        ELOG_REPORT_SYS_ERROR(uname, "Failed to get Linux version information");
    } else {
        // now get distribution (Ubuntu, RHEL, etc.)
        elog_strncpy(sOsName, buf.sysname, OS_NAME_MAX);
        std::string dist;
        if (getLinuxDistribution(dist)) {
            // NOTE: strncat() result is always null-terminated
            strncat(sOsName, " ", OS_NAME_MAX - strlen(sOsName));
            strncat(sOsName, dist.c_str(), OS_NAME_MAX - strlen(sOsName) - 1);
        }
        elog_strncpy(sOsVersion, buf.release, OS_VERSION_MAX);
    }
#endif
}

static void initProgName() {
    // set some default in case of error
    elog_strncpy(sProgName, "N/A", PROG_NAME_MAX);

    // get executable file name
#ifdef ELOG_WINDOWS
    DWORD pathLen = GetModuleFileNameA(nullptr, sProgName, PROG_NAME_MAX);
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
    ssize_t res = read(fd, sProgName, PROG_NAME_MAX);
    if (res == ((ssize_t)-1)) {
        ELOG_REPORT_ERROR("Failed to read from /proc/self/cmdline for reading: %d", errno);
        close(fd);
        return;
    }
    close(fd);

    // search for null and if not found then set one
    bool nullFound = false;
    for (uint32_t i = 0; i < PROG_NAME_MAX; ++i) {
        if (sProgName[i] == 0) {
            nullFound = true;
            break;
        }
    }
    if (!nullFound) {
        sProgName[PROG_NAME_MAX - 1] = 0;
    }
    const char slash = '/';
#endif

    // platform-agnostic part:
    // now search for last slash and pull string back
    char* slashPos = strrchr(sProgName, slash);
    if (slashPos != nullptr) {
        memmove(sProgName, slashPos + 1, strlen(slashPos + 1) + 1);
    }

    // finally remove extension
    char* dotPtr = strrchr(sProgName, '.');
    if (dotPtr != nullptr) {
        size_t dotPos = (size_t)(dotPtr - sProgName);
        sProgName[dotPos] = 0;
    }
}

extern bool initFieldSelectors() {
    if (!elogCreateTls(sThreadNameKey, cleanupThreadName)) {
        ELOG_REPORT_ERROR(
            "Failed to create thread name map TLS key, during initialization of field selectors");
        return false;
    }

    if (!sThreadNameMap.initialize(getMaxThreads() * ELOG_THREAD_HASH_MAP_FACTOR)) {
        ELOG_REPORT_ERROR(
            "Failed to initialize concurrent thread name map, during initialization of field "
            "selectors");
        elogDestroyTls(sThreadNameKey);
        return false;
    }

    if (!applyFieldSelectorConstructorRegistration()) {
        sThreadNameMap.destroy();
        elogDestroyTls(sThreadNameKey);
        return false;
    }

    // initialize host name
    initHostName();

    // initialize user name
    initUserName();

    // initialize OS name and version
    initOsNameAndVersion();

    // initialize program name
    initProgName();

    // initialize pid
#ifdef ELOG_WINDOWS
    pid = GetCurrentProcessId();
#else
    pid = getpid();
#endif  // ELOG_WINDOWS
    return true;
}

void termFieldSelectors() {
    sFieldSelectorConstructorMap.clear();
    sThreadNameMap.destroy();
    elogDestroyTls(sThreadNameKey);
}

const char* getHostName() { return sHostName; }

const char* getUserName() { return sUserName; }

extern const char* getOsName() { return sOsName; }

extern const char* getOsVersion() { return sOsVersion; }

extern const char* getAppName() { return sAppName; }

const char* getProgramName() { return sProgName; }

uint32_t getProcessIdField() { return (uint32_t)pid; }

void setAppNameField(const char* appName) {
    elog_strncpy(sAppName, appName, APP_NAME_MAX);
#ifdef ELOG_ENABLE_LIFE_SIGN
    reportAppNameLifeSign(appName);
#endif
}

bool setCurrentThreadNameField(const char* threadName) {
    // first check for duplicate name
    elog_thread_id_t threadId = getCurrentThreadId();
    ELogThreadData threadData(threadId);
    {
        std::unique_lock<std::mutex> lock(sLock);
        if (!sThreadDataMap.insert(ELogThreadDataMap::value_type(threadName, threadData)).second) {
            ELOG_REPORT_ERROR(
                "Cannot set current thread name to '%s', name is already used by another thread",
                threadName);
            return false;
        }
    }

// now we can save the name and add to the id/name map
#ifdef ELOG_MSVC
    char* threadNameDup = _strdup(threadName);
#else
    char* threadNameDup = strdup(threadName);
#endif
    if (threadNameDup == nullptr) {
        ELOG_REPORT_ERROR("Failed to set current thread name for log reporting, out of memory");
        std::unique_lock<std::mutex> lock(sLock);
        ELogThreadDataMap::iterator itr = sThreadDataMap.find(threadName);
        if (itr != sThreadDataMap.end()) {
            sThreadDataMap.erase(itr);
        }
        return false;
    }
    sThreadName = threadNameDup;

    // this is required to trigger cleanup when thread ends
    if (!elogSetTls(sThreadNameKey, (void*)(uint64_t)threadId)) {
        ELOG_REPORT_ERROR("Failed to setup TLS cleanup for current thread name");
        free(threadNameDup);
        sThreadName = nullptr;
        std::unique_lock<std::mutex> lock(sLock);
        ELogThreadDataMap::iterator itr = sThreadDataMap.find(threadName);
        if (itr != sThreadDataMap.end()) {
            sThreadDataMap.erase(itr);
        }
        return false;
    }

    // save thread id/name (allocated on heap) in a global map
    uint32_t entryId = sThreadNameMap.setItem((uint64_t)threadId, threadNameDup);

    // infrom life-sign of current thread name
#ifdef ELOG_ENABLE_LIFE_SIGN
    reportCurrentThreadNameLifeSign(threadId, threadName);
#endif
    ELOG_REPORT_DEBUG("Thread name set to %s at entry id %u", threadName, entryId);
    return true;
}

const char* getThreadNameField(uint32_t threadId) {
    const char* threadName = "";
    // if failed it will remain empty, so we don't need to check return value
    if (sThreadNameMap.getItem((uint64_t)threadId, threadName) == ELOG_INVALID_CHT_ENTRY_ID) {
        ELOG_REPORT_DEBUG("Could not find thread name by id %u", threadId);
    }
    return threadName;
}

#ifdef ELOG_ENABLE_LIFE_SIGN
bool setCurrentThreadNotifierImpl(dbgutil::ThreadNotifier* notifier) {
    if (sThreadName == nullptr) {
        ELOG_REPORT_ERROR(
            "Cannot set current thread notifier for life-sign reports, missing current thread "
            "name");
        return false;
    }
    return setThreadNotifierImpl(sThreadName, notifier);
}

extern bool setThreadNotifierImpl(const char* threadName, dbgutil::ThreadNotifier* notifier) {
    std::unique_lock<std::mutex> lock(sLock);
    ELogThreadDataMap::iterator itr = sThreadDataMap.find(threadName);
    if (itr == sThreadDataMap.end()) {
        return false;
    }
    itr->second.m_notifier = notifier;
    return true;
}

bool getThreadDataByName(const char* threadName, uint32_t& threadId,
                         dbgutil::ThreadNotifier*& notifier) {
    std::unique_lock<std::mutex> lock(sLock);
    ELogThreadDataMap::iterator itr = sThreadDataMap.find(threadName);
    if (itr != sThreadDataMap.end()) {
        threadId = itr->second.m_threadId;
        notifier = itr->second.m_notifier;
        return true;
    }
    return false;
}

void getThreadDataByNameRegEx(const char* threadNameRegEx, ThreadDataMap& threadIds) {
    std::regex pattern(threadNameRegEx);
    std::unique_lock<std::mutex> lock(sLock);
    ELogThreadDataMap::iterator itr = sThreadDataMap.begin();
    while (itr != sThreadDataMap.end()) {
        if (std::regex_match(itr->first, pattern)) {
            threadIds.insert(ThreadDataMap::value_type(itr->second.m_threadId,
                                                       {itr->first, itr->second.m_notifier}));
        }
        ++itr;
    }
}
#endif

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

void ELogTimeSelector::selectField(const ELogRecord& record, ELogFieldReceptor* receptor) {
    ELogTimeBuffer timeBuffer;
    size_t len = elogTimeToString(record.m_logTime, timeBuffer);
    receptor->receiveTimeField(getTypeId(), record.m_logTime, timeBuffer.m_buffer, m_fieldSpec,
                               len);
}

void ELogTimeEpochSelector::selectField(const ELogRecord& record, ELogFieldReceptor* receptor) {
    uint64_t unixTimeMicros = elogTimeToUnixTimeNanos(record.m_logTime) / 1000ull;
    if (receptor->getFieldReceiveStyle() == ELogFieldReceptor::ReceiveStyle::RS_BY_NAME) {
        receptor->receiveTimeEpoch(getTypeId(), unixTimeMicros, m_fieldSpec);
    } else {
        receptor->receiveIntField(getTypeId(), unixTimeMicros, m_fieldSpec);
    }
}

void ELogHostNameSelector::selectField(const ELogRecord& record, ELogFieldReceptor* receptor) {
    if (receptor->getFieldReceiveStyle() == ELogFieldReceptor::ReceiveStyle::RS_BY_NAME) {
        receptor->receiveHostName(getTypeId(), sHostName, m_fieldSpec);
    } else {
        receptor->receiveStringField(getTypeId(), sHostName, m_fieldSpec);
    }
}

void ELogUserNameSelector::selectField(const ELogRecord& record, ELogFieldReceptor* receptor) {
    if (receptor->getFieldReceiveStyle() == ELogFieldReceptor::ReceiveStyle::RS_BY_NAME) {
        receptor->receiveUserName(getTypeId(), sUserName, m_fieldSpec);
    } else {
        receptor->receiveStringField(getTypeId(), sUserName, m_fieldSpec);
    }
}

void ELogOsNameSelector::selectField(const ELogRecord& record, ELogFieldReceptor* receptor) {
    if (receptor->getFieldReceiveStyle() == ELogFieldReceptor::ReceiveStyle::RS_BY_NAME) {
        receptor->receiveOsName(getTypeId(), sOsName, m_fieldSpec);
    } else {
        receptor->receiveStringField(getTypeId(), sOsName, m_fieldSpec);
    }
}

void ELogOsVersionSelector::selectField(const ELogRecord& record, ELogFieldReceptor* receptor) {
    if (receptor->getFieldReceiveStyle() == ELogFieldReceptor::ReceiveStyle::RS_BY_NAME) {
        receptor->receiveOsVersion(getTypeId(), sOsVersion, m_fieldSpec);
    } else {
        receptor->receiveStringField(getTypeId(), sOsVersion, m_fieldSpec);
    }
}

void ELogAppNameSelector::selectField(const ELogRecord& record, ELogFieldReceptor* receptor) {
    if (receptor->getFieldReceiveStyle() == ELogFieldReceptor::ReceiveStyle::RS_BY_NAME) {
        receptor->receiveAppName(getTypeId(), sAppName, m_fieldSpec);
    } else {
        receptor->receiveStringField(getTypeId(), sAppName, m_fieldSpec);
    }
}

void ELogProgramNameSelector::selectField(const ELogRecord& record, ELogFieldReceptor* receptor) {
    if (receptor->getFieldReceiveStyle() == ELogFieldReceptor::ReceiveStyle::RS_BY_NAME) {
        receptor->receiveProgramName(getTypeId(), sProgName, m_fieldSpec);
    } else {
        receptor->receiveStringField(getTypeId(), sProgName, m_fieldSpec);
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
    const char* threadName = getThreadNameField(record.m_threadId);
    if (receptor->getFieldReceiveStyle() == ELogFieldReceptor::ReceiveStyle::RS_BY_NAME) {
        receptor->receiveThreadName(getTypeId(), threadName, m_fieldSpec);
    } else {
        receptor->receiveStringField(getTypeId(), threadName, m_fieldSpec);
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
    // if log record is in binary form, it must be resolved
    if (record.m_flags & ELOG_RECORD_BINARY) {
#ifdef ELOG_ENABLE_FMT_LIB
        ELogBuffer logBuffer;
        if (ELogLogger::resolveLogRecord(record, logBuffer)) {
            if (receptor->getFieldReceiveStyle() == ELogFieldReceptor::ReceiveStyle::RS_BY_NAME) {
                receptor->receiveLogMsg(getTypeId(), logBuffer.getRef(), m_fieldSpec);
            } else {
                receptor->receiveStringField(getTypeId(), logBuffer.getRef(), m_fieldSpec,
                                             logBuffer.getOffset());
            }
        }
#endif
    } else {
        if (receptor->getFieldReceiveStyle() == ELogFieldReceptor::ReceiveStyle::RS_BY_NAME) {
            receptor->receiveLogMsg(getTypeId(), record.m_logMsg, m_fieldSpec);
        } else {
            receptor->receiveStringField(getTypeId(), record.m_logMsg, m_fieldSpec,
                                         record.m_logMsgLen);
        }
    }
}

void ELogFormatSelector::selectField(const ELogRecord& record, ELogFieldReceptor* receptor) {
    receptor->receiveStringField(getTypeId(), "", m_fieldSpec, 0);
}

ELogIfSelector::~ELogIfSelector() {
    if (m_cond != nullptr) {
        delete m_cond;
        m_cond = nullptr;
    }
    if (m_trueSelector != nullptr) {
        delete m_trueSelector;
        m_trueSelector = nullptr;
    }
    if (m_falseSelector != nullptr) {
        delete m_falseSelector;
        m_falseSelector = nullptr;
    }
}

void ELogIfSelector::selectField(const ELogRecord& record, ELogFieldReceptor* receptor) {
    if (m_cond->filterLogRecord(record)) {
        m_trueSelector->selectField(record, receptor);
    } else if (m_falseSelector != nullptr) {
        m_falseSelector->selectField(record, receptor);
    }
}

class ELogFieldContainer : public ELogFieldReceptor {
public:
    ELogFieldContainer() : ELogFieldReceptor(ReceiveStyle::RS_BY_TYPE) {}
    ELogFieldContainer(const ELogFieldContainer&) = delete;
    ELogFieldContainer(ELogFieldContainer&&) = delete;
    ELogFieldContainer& operator=(const ELogFieldContainer&) = delete;
    ~ELogFieldContainer() final {}

    /** @brief Receives a string log record field. */
    void receiveStringField(uint32_t typeId, const char* value, const ELogFieldSpec& fieldSpec,
                            size_t length = 0) override {
        m_stringValue = value;
        m_length = length;
        m_fieldType = ELogFieldType::FT_TEXT;
    }

    /** @brief Receives an integer log record field. */
    void receiveIntField(uint32_t typeId, uint64_t value, const ELogFieldSpec& fieldSpec) override {
        m_intValue = value;
        m_fieldType = ELogFieldType::FT_INT;
    }

    /** @brief Receives a time log record field. */
    void receiveTimeField(uint32_t typeId, const ELogTime& logTime, const char* timeStr,
                          const ELogFieldSpec& fieldSpec, size_t length = 0) override {
        m_timeValue = logTime;
        m_fieldType = ELogFieldType::FT_DATETIME;
    }

    /** @brief Receives a log level log record field. */
    void receiveLogLevelField(uint32_t typeId, ELogLevel logLevel,
                              const ELogFieldSpec& fieldSpec) override {
        m_logLevel = logLevel;
        m_fieldType = ELogFieldType::FT_LOG_LEVEL;
    }

    /**
     * @brief Checks whether the value contained by this container equals the value contained by
     * another container.
     */
    inline bool equals(const ELogFieldContainer& value) const {
        // should be checked during switch condition construction
        assert(m_fieldType == value.m_fieldType);
        switch (m_fieldType) {
            case ELogFieldType::FT_TEXT:
                return strcmp(m_stringValue, value.m_stringValue) == 0;

            case ELogFieldType::FT_INT:
                return m_intValue == value.m_intValue;

            case ELogFieldType::FT_DATETIME:
                return elogTimeEquals(m_timeValue, value.m_timeValue);

            case ELogFieldType::FT_LOG_LEVEL:
                return m_logLevel == value.m_logLevel;

            case ELogFieldType::FT_FORMAT:
                // switch-expr should never evaluate format expression
                ELOG_REPORT_WARN(
                    "Attempt to perform conditional token evaluation on format field selector "
                    "ignored");
                return false;

            default:
                // should never happen
                assert(false);
                return false;
        }
    }

private:
    const char* m_stringValue;
    size_t m_length;
    uint64_t m_intValue;
    ELogTime m_timeValue;
    ELogLevel m_logLevel;
    ELogFieldType m_fieldType;
};

ELogSwitchSelector::~ELogSwitchSelector() {
    if (m_valueExpr != nullptr) {
        delete m_valueExpr;
        m_valueExpr = nullptr;
    }
    for (auto& entry : m_cases) {
        delete entry.first;
        delete entry.second;
    }
    m_cases.clear();
    if (m_defaultFieldSelector != nullptr) {
        delete m_defaultFieldSelector;
        m_defaultFieldSelector = nullptr;
    }
}

void ELogSwitchSelector::selectField(const ELogRecord& record, ELogFieldReceptor* receptor) {
    // select the record value into a field container
    ELogFieldContainer valueContainer;
    m_valueExpr->selectField(record, &valueContainer);
    bool fieldSelected = false;
    for (auto& caseExpr : m_cases) {
        ELogFieldContainer caseValueContainer;
        caseExpr.first->selectField(record, &caseValueContainer);
        if (valueContainer.equals(caseValueContainer)) {
            caseExpr.second->selectField(record, receptor);
            fieldSelected = true;
            break;
        }
    }

    // use default clause if defined and no case was matched
    if (!fieldSelected && m_defaultFieldSelector != nullptr) {
        m_defaultFieldSelector->selectField(record, receptor);
    }
}

ELogExprSwitchSelector::~ELogExprSwitchSelector() {
    for (auto& entry : m_cases) {
        delete entry.first;
        delete entry.second;
    }
    m_cases.clear();
    if (m_defaultFieldSelector != nullptr) {
        delete m_defaultFieldSelector;
        m_defaultFieldSelector = nullptr;
    }
}

void ELogExprSwitchSelector::selectField(const ELogRecord& record, ELogFieldReceptor* receptor) {
    // match case expression and execute the format selector
    bool fieldSelected = false;
    for (auto& caseExpr : m_cases) {
        if (caseExpr.first->filterLogRecord(record)) {
            caseExpr.second->selectField(record, receptor);
            fieldSelected = true;
            break;
        }
    }

    // use default clause if defined and no case was matched
    if (!fieldSelected && m_defaultFieldSelector != nullptr) {
        m_defaultFieldSelector->selectField(record, receptor);
    }
}

void ELogConstStringSelector::selectField(const ELogRecord& record, ELogFieldReceptor* receptor) {
    // we should get here only when passing value to field container
    receptor->receiveStringField(getTypeId(), m_constString.c_str(), m_fieldSpec,
                                 m_constString.length());
}

void ELogConstIntSelector::selectField(const ELogRecord& record, ELogFieldReceptor* receptor) {
    // we should get here only when passing value to field container
    receptor->receiveIntField(getTypeId(), m_constInt, m_fieldSpec);
}

void ELogConstTimeSelector::selectField(const ELogRecord& record, ELogFieldReceptor* receptor) {
    // we should get here only when passing value to field container
    receptor->receiveTimeField(getTypeId(), m_constTime, m_timeStr.c_str(), m_fieldSpec);
}

void ELogConstLogLevelSelector::selectField(const ELogRecord& record, ELogFieldReceptor* receptor) {
    // we should get here only when passing value to field container
    receptor->receiveLogLevelField(getTypeId(), m_constLevel, m_fieldSpec);
}

}  // namespace elog
