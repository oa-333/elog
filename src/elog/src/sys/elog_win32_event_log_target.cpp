#include "sys/elog_win32_event_log_target.h"

#ifdef ELOG_WINDOWS

#include "elog_field_selector.h"
#include "elog_field_selector_internal.h"
#include "elog_report.h"

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogWin32EventLogTarget)

ELOG_IMPLEMENT_LOG_TARGET(ELogWin32EventLogTarget)

bool ELogWin32EventLogTarget::startLogTarget() {
    if (m_eventSourceName.empty()) {
        m_eventSourceName = getAppNameField();
        if (m_eventSourceName.empty()) {
            m_eventSourceName = getProgramNameField();
            if (m_eventSourceName.empty()) {
                m_eventSourceName = "elog";
            }
        }
    }

    // Register the event source
    m_eventLogHandle = RegisterEventSourceA(nullptr, m_eventSourceName.c_str());
    if (m_eventLogHandle == nullptr) {
        ELOG_REPORT_WIN32_ERROR(RegisterEventSource,
                                "Failed to register Windows event source by name %s",
                                m_eventSourceName.c_str());
        return false;
    }

    return true;
}

bool ELogWin32EventLogTarget::stopLogTarget() {
    if (m_eventLogHandle != nullptr) {
        if (!DeregisterEventSource(m_eventLogHandle)) {
            ELOG_REPORT_WIN32_ERROR(DeregisterEventSource,
                                    "Failed to register Windows event source by name %s",
                                    m_eventSourceName.c_str());
            return false;
        }
        m_eventLogHandle = nullptr;
    }

    return true;
}

bool ELogWin32EventLogTarget::writeLogRecord(const ELogRecord& logRecord, uint64_t& bytesWritten) {
    WORD eventType = 0;
    bytesWritten = 0;
    if (logLevelToEventType(logRecord.m_logLevel, eventType)) {
        std::string logMsg;
        formatLogMsg(logRecord, logMsg);
        const char* msg = logMsg.c_str();
        if (!ReportEventA(m_eventLogHandle,           // Event log handle
                          EVENTLOG_INFORMATION_TYPE,  // Event type
                          0,                          // Category
                          m_eventId,                  // Event identifier
                          nullptr,                    // No user SID
                          1,                          // Number of strings
                          0,                          // No binary data
                          &msg,                       // Array of strings
                          nullptr                     // No binary data
                          )) {
            // NOTE: this error report will be directed only to stderr, and will not be reported
            // recursively into this log target
            ELOG_REPORT_MODERATE_WIN32_ERROR_DEFAULT(
                "Failed to report to event log message of size %zu", logMsg.c_str());

            return false;
        }
        bytesWritten = logMsg.length();
    }
    return true;
}

bool ELogWin32EventLogTarget::flushLogTarget() { return true; }

bool ELogWin32EventLogTarget::logLevelToEventType(ELogLevel logLevel, WORD& eventType) {
    switch (logLevel) {
        case ELEVEL_FATAL:
        case ELEVEL_ERROR:
            eventType = EVENTLOG_ERROR_TYPE;
            break;

        case ELEVEL_WARN:
        case ELEVEL_NOTICE:
            eventType = EVENTLOG_WARNING_TYPE;
            break;

        case ELEVEL_INFO:
            eventType = EVENTLOG_INFORMATION_TYPE;
            break;

        case ELEVEL_TRACE:
        case ELEVEL_DEBUG:
        case ELEVEL_DIAG:
        default:
            // NOTE: trace/debug/diagnostic level is not reported to avoid event log flooding
            return false;
    }
    return true;
}

}  // namespace elog

#endif  // ELOG_LINUX