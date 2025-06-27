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
#include <sys/utsname.h>
#include <unistd.h>
#endif

#define OS_NAME_MAX 256
#define OS_VERSION_MAX 256
#define APP_NAME_MAX 256
#define PROG_NAME_MAX 256
#define THREAD_NAME_MAX 256

#include <climits>
#include <cstring>
#include <format>
#include <iomanip>
#include <unordered_map>

#include "elog_common.h"
#include "elog_error.h"
#include "elog_system.h"

namespace elog {

static char sHostName[HOST_NAME_MAX];
static char sUserName[LOGIN_NAME_MAX];
static char sOsName[OS_NAME_MAX];
static char sOsVersion[OS_VERSION_MAX];
static char sAppName[APP_NAME_MAX];
static char sProgName[PROG_NAME_MAX];
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
        strcpy(sHostName, "<N/A>");
    }
#else
    if (gethostname(sHostName, HOST_NAME_MAX) != 0) {
        strcpy(sHostName, "<N/A>");
    }
#endif
}

static void initUserName() {
#ifdef ELOG_WINDOWS
    DWORD len = LOGIN_NAME_MAX;
    if (!GetUserNameA(sUserName, &len)) {
        strcpy(sUserName, "<N/A>");
    }
#else  // ELOG_WINDOWS
    if (getlogin_r(sUserName, LOGIN_NAME_MAX) != 0) {
        strcpy(sUserName, "<N/A>");
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

static const char* getWin32SuiteName(LPOSVERSIONINFOEXA verInfo) {
    // currently we don't support this, is it really needed?
    return "";
}
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

    // format OS name
    std::stringstream s;
    s << getWin32OSName(&verInfo);
#ifdef ELOG_MINGW
    s << " MSYS2";
#endif
    std::string osName = s.str();
    elog_strncpy(sOsName, osName.c_str(), OS_NAME_MAX);

    // format version
    s.str(std::string());  // clear string stream contents
    s << verInfo.dwMajorVersion << "." << verInfo.dwMinorVersion << "." << verInfo.dwBuildNumber
      << " " << verInfo.szCSDVersion << getWin32SuiteName(&verInfo) << " "
      << getWin32ProductName(&verInfo);
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
    strcpy(sProgName, "N/A");

    // get executable file name
#ifdef ELOG_WINDOWS
    DWORD pathLen = GetModuleFileNameA(NULL, sProgName, PROG_NAME_MAX);
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
        uint32_t dotPos = dotPtr - sProgName;
        sProgName[dotPos] = 0;
    }
}

extern bool initFieldSelectors() {
    if (!applyFieldSelectorConstructorRegistration()) {
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

void termFieldSelectors() { sFieldSelectorConstructorMap.clear(); }

const char* getHostName() { return sHostName; }

const char* getUserName() { return sUserName; }

extern const char* getOsName() { return sOsName; }

extern const char* getOsVersion() { return sOsVersion; }

extern const char* getAppName() { return sAppName; }

const char* getProgramName() { return sProgName; }

void setAppNameField(const char* appName) { elog_strncpy(sAppName, appName, APP_NAME_MAX); }

void setCurrentThreadNameField(const char* threadName) {
    elog_strncpy(sThreadName, threadName, THREAD_NAME_MAX);
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

void ELogTimeSelector::selectField(const ELogRecord& record, ELogFieldReceptor* receptor) {
    ELogTimeBuffer timeBuffer;
    size_t len = elogTimeToString(record.m_logTime, timeBuffer);
    receptor->receiveTimeField(getTypeId(), record.m_logTime, timeBuffer.m_buffer, m_fieldSpec,
                               len);
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
