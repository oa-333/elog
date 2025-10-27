#ifndef __ELOG_WIN32_EVENT_LOG_TARGET_H__
#define __ELOG_WIN32_EVENT_LOG_TARGET_H__

#include "elog_def.h"

#ifdef ELOG_WINDOWS
#include "elog_target.h"

#ifdef ELOG_MINGW
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

#define ELOG_DEFAULT_WIN32_EVENT_LOG_ID 0x1000

namespace elog {

class ELogWin32EventLogTarget : public ELogTarget {
public:
    /**
     * @brief Construct a new Windows Event Log target
     *
     * @param eventSourceName The event source name as it will appear in the event log reports.
     * If this parameter is left empty, then the application name as configured by the user via @ref
     * elog::setAppName() will be used. If none was set, then the program name, as extracted
     * from the current executable image, will be used instead. If all fails the name "elog" will be
     * used as a last resort.
     * @param eventId The event id to use in the event log report. Since no message
     * file/resource-dll is involved in the reports, this is solely used for searching/identifying
     * events in the event viewer.
     * @param enableStats Specifies whether to collect statistics for this og target.
     */
    ELogWin32EventLogTarget(const char* eventSourceName = "",
                            uint32_t eventId = ELOG_DEFAULT_WIN32_EVENT_LOG_ID,
                            bool enableStats = false)
        : ELogTarget("win32eventlog", nullptr, enableStats),
          m_eventSourceName(eventSourceName),
          m_eventLogHandle(nullptr),
          m_eventId(eventId) {
        setName("win32eventlog");
    }
    ELogWin32EventLogTarget(const ELogWin32EventLogTarget&) = delete;
    ELogWin32EventLogTarget(ELogWin32EventLogTarget&&) = delete;
    ELogWin32EventLogTarget& operator=(const ELogWin32EventLogTarget&) = delete;

    ELOG_DECLARE_LOG_TARGET(ELogWin32EventLogTarget)

protected:
    /** @brief Order the log target to start (required for threaded targets). */
    bool startLogTarget() final;

    /** @brief Order the log target to stop (required for threaded targets). */
    bool stopLogTarget() final;

    /** @brief Sends a log record to a log target. */
    uint32_t writeLogRecord(const ELogRecord& logRecord) final;

    /** @brief Orders a buffered log target to flush it log messages. */
    bool flushLogTarget() final;

private:
    bool logLevelToEventType(ELogLevel logLevel, WORD& eventType);

    std::string m_eventSourceName;
    HANDLE m_eventLogHandle;
    uint32_t m_eventId;
};

}  // namespace elog

#endif  // ELOG_WINDOWS

#endif  // __ELOG_WIN32_EVENT_LOG_TARGET_H__