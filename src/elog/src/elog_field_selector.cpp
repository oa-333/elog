#include "elog_field_selector.h"

#ifdef __WIN32__
#include <winsock2.h>
#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX 256
#endif
#ifndef LOGIN_NAME_MAX
#define LOGIN_NAME_MAX 256
#endif
#else
#include <unistd.h>
#endif

#include <climits>

#include "elog_system.h"

// TODO: we would like to have host name, user name and process id retrieves once at startup and
// cached for later use

namespace elog {

void ELogStaticTextSelector::selectField(const ELogRecord& record, std::stringstream& msgStream) {
    msgStream << m_text;
}

void ELogRecordIdSelector::selectField(const ELogRecord& record, std::stringstream& msgStream) {
    msgStream << std::to_string(record.m_logRecordId);
}

void ELogTimeSelector::selectField(const ELogRecord& record, std::stringstream& msgStream) {
    time_t timer = record.m_logTime.tv_sec;
    struct tm* tm_info = localtime(&timer);
    char buffer[64];
    size_t offset = strftime(buffer, 64, "%Y-%m-%d %H:%M:%S.", tm_info);
    offset += sprintf(buffer + offset, "%.3u", (unsigned)(record.m_logTime.tv_usec / 1000));
    msgStream << buffer;
}

void ELogHostNameSelector::selectField(const ELogRecord& record, std::stringstream& msgStream) {
    static thread_local char hostName[HOST_NAME_MAX];
    static thread_local bool hostNameInit = false;
    if (!hostNameInit) {
        if (gethostname(hostName, HOST_NAME_MAX) != 0) {
            strcpy(hostName, "<N/A>");
        }
        hostNameInit = true;
    }
    msgStream << hostName;
}

void ELogUserNameSelector::selectField(const ELogRecord& record, std::stringstream& msgStream) {
    static thread_local char userName[LOGIN_NAME_MAX];
    static thread_local bool userNameInit = false;
    if (!userNameInit) {
#ifdef __WIN32__
        DWORD len = 256;
        if (!GetUserNameA(userName, &len)) {
            strcpy(userName, "<N/A>");
        }
#else
        if (getlogin_r(userName, LOGIN_NAME_MAX) != 0) {
            strcpy(userName, "<N/A>");
        }
#endif
        userNameInit = true;
    }
    msgStream << userName;
}

void ELogProcessIdSelector::selectField(const ELogRecord& record, std::stringstream& msgStream) {
#ifdef __WIN32__
    // cache this call (at least per-thread) by having a thread local variable
    static thread_local DWORD pid = GetCurrentProcessId();
    msgStream << std::to_string(pid);
#else
    // cache this call (at least per-thread) by having a thread local variable
    static thread_local pid_t pid = getpid();
    msgStream << std::to_string(pid);
#endif
}

void ELogThreadIdSelector::selectField(const ELogRecord& record, std::stringstream& msgStream) {
    msgStream << std::to_string(record.m_threadId);
}

void ELogSourceSelector::selectField(const ELogRecord& record, std::stringstream& msgStream) {
    ELogSource* logSource = ELogSystem::getLogSource(record.m_sourceId);
    if (logSource != nullptr) {
        msgStream << logSource->getQualifiedName();
    } else {
        msgStream << "<N/A>";
    }
}

void ELogLevelSelector::selectField(const ELogRecord& record, std::stringstream& msgStream) {
    msgStream << elogLevelToStr(record.m_logLevel);
}

void ELogMsgSelector::selectField(const ELogRecord& record, std::stringstream& msgStream) {
    msgStream << record.m_logMsg;
}

void selectField(const ELogRecord& record, std::stringstream& msgStream) {
    msgStream << record.m_logMsg;
}

}  // namespace elog
