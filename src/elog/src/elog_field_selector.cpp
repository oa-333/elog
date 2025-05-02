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

#include <climits>
#include <cstring>
#include <iomanip>

#include "elog_system.h"

namespace elog {

static char hostName[HOST_NAME_MAX];
static char userName[LOGIN_NAME_MAX];
static char progName[PROG_NAME_MAX];

#ifdef ELOG_WINDOWS
static DWORD pid = 0;
#else
static pid_t pid = 0;
#endif

static void getProgName() {
    // set some default in case of error
    strcpy(progName, "N/A");

    // get executable file name
#ifdef ELOG_WINDOWS
    char modulePath[PROG_NAME_MAX];
    DWORD pathLen = GetModuleFileNameA(NULL, progName, PROG_NAME_MAX);
    if (pathLen == 0) {
        fprintf(stderr, "WARNING: Failed to get executable file name: %u\n", GetLastError());
        return;
    }
    const char slash = '\\';
#else
    // can get it only from /proc/self/cmdline
    int fd = open("/proc/self/cmdline", O_RDONLY, 0);
    if (fd == -1) {
        fprintf(stderr, "Failed to open /proc/self/cmdline for reading: %d\n", errno);
        return;
    }

    // read as many bytes as possible, the program name ends with a null (then program arguments
    // follow, but we don't care about them)
    ssize_t res = read(fd, progName, PROG_NAME_MAX);
    if (res == ((ssize_t)-1)) {
        fprintf(stderr, "Failed to read from /proc/self/cmdline for reading: %d\n", errno);
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
}

extern void initFieldSelectors() {
    // initialize host name
    if (gethostname(hostName, HOST_NAME_MAX) != 0) {
        strcpy(hostName, "<N/A>");
    }

    // initialize user name
#ifdef ELOG_WINDOWS
    DWORD len = 256;
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
}

const char* getProgramName() { return progName; }

void ELogFieldSelector::applyJustify(std::stringstream& msgStream) {
    if (m_justify > 0) {
        // left justify
        msgStream << std::setw(m_justify) << std::left;
    } else if (m_justify < 0) {
        // right justify
        msgStream << std::setw(-m_justify) << std::right;
    } else {
        // no justify
        msgStream << std::setw(0);
    }
}

void ELogStaticTextSelector::selectField(const ELogRecord& record, std::stringstream& msgStream) {
    applyJustify(msgStream);
    msgStream << m_text;
}

void ELogRecordIdSelector::selectField(const ELogRecord& record, std::stringstream& msgStream) {
    applyJustify(msgStream);
    msgStream << std::to_string(record.m_logRecordId);
}

void ELogTimeSelector::selectField(const ELogRecord& record, std::stringstream& msgStream) {
    const uint32_t bufSize = 64;
    char buffer[bufSize];
#ifdef ELOG_MSVC
    size_t offset = snprintf(buffer, bufSize, "%u-%.2u-%.2u %.2u:%.2u:%.2u.%.3u",
                             record.m_logTime.wYear, record.m_logTime.wMonth, record.m_logTime.wDay,
                             record.m_logTime.wHour, record.m_logTime.wMinute,
                             record.m_logTime.wSecond, record.m_logTime.wMilliseconds);
#else
    time_t timer = record.m_logTime.tv_sec;
    struct tm* tm_info = localtime(&timer);
    size_t offset = strftime(buffer, 64, "%Y-%m-%d %H:%M:%S.", tm_info);
    offset += snprintf(buffer + offset, bufSize - offset, "%.3u",
                       (unsigned)(record.m_logTime.tv_usec / 1000));
#endif

    applyJustify(msgStream);
    msgStream << buffer;
}

void ELogHostNameSelector::selectField(const ELogRecord& record, std::stringstream& msgStream) {
    applyJustify(msgStream);
    msgStream << hostName;
}

void ELogUserNameSelector::selectField(const ELogRecord& record, std::stringstream& msgStream) {
    applyJustify(msgStream);
    msgStream << userName;
}

void ELogProgramNameSelector::selectField(const ELogRecord& record, std::stringstream& msgStream) {
    applyJustify(msgStream);
    msgStream << progName;
}

void ELogProcessIdSelector::selectField(const ELogRecord& record, std::stringstream& msgStream) {
    applyJustify(msgStream);
    msgStream << std::to_string(pid);
}

void ELogThreadIdSelector::selectField(const ELogRecord& record, std::stringstream& msgStream) {
    applyJustify(msgStream);
    msgStream << std::to_string(record.m_threadId);
}

void ELogSourceSelector::selectField(const ELogRecord& record, std::stringstream& msgStream) {
    applyJustify(msgStream);
    ELogSource* logSource = ELogSystem::getLogSource(record.m_sourceId);
    if (logSource != nullptr && (*logSource->getQualifiedName() != 0)) {
        msgStream << "<" << logSource->getQualifiedName() << ">";
    }
}

void ELogModuleSelector::selectField(const ELogRecord& record, std::stringstream& msgStream) {
    applyJustify(msgStream);
    ELogSource* logSource = ELogSystem::getLogSource(record.m_sourceId);
    if (logSource != nullptr) {
        msgStream << logSource->getModuleName();
    } else {
        msgStream << "<N/A>";
    }
}

void ELogLevelSelector::selectField(const ELogRecord& record, std::stringstream& msgStream) {
    applyJustify(msgStream);
    msgStream << elogLevelToStr(record.m_logLevel);
}

void ELogMsgSelector::selectField(const ELogRecord& record, std::stringstream& msgStream) {
    applyJustify(msgStream);
    msgStream << record.m_logMsg;
}

}  // namespace elog
