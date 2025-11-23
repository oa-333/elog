#include "elog_test_common.h"

#ifdef ELOG_WINDOWS
// help intellisense
#include <Windows.h>

#define MAX_TIMESTAMP_LEN 64

const char* pEventTypeNames[] = {"Error", "Warning", "Informational", "Audit Success",
                                 "Audit Failure"};

DWORD GetEventTypeName(DWORD EventType) {
    DWORD index = 0;

    switch (EventType) {
        case EVENTLOG_ERROR_TYPE:
            index = 0;
            break;
        case EVENTLOG_WARNING_TYPE:
            index = 1;
            break;
        case EVENTLOG_INFORMATION_TYPE:
            index = 2;
            break;
        case EVENTLOG_AUDIT_SUCCESS:
            index = 3;
            break;
        case EVENTLOG_AUDIT_FAILURE:
            index = 4;
            break;
    }

    return index;
}

void GetTimestamp(const DWORD Time, char displayString[]) {
    ULONGLONG ullTimeStamp = 0;
    ULONGLONG SecsTo1970 = 116444736000000000;
    SYSTEMTIME st;
    FILETIME ft, ftLocal;

    ullTimeStamp = Int32x32To64(Time, 10000000) + SecsTo1970;
    ft.dwHighDateTime = (DWORD)((ullTimeStamp >> 32) & 0xFFFFFFFF);
    ft.dwLowDateTime = (DWORD)(ullTimeStamp & 0xFFFFFFFF);

    FileTimeToLocalFileTime(&ft, &ftLocal);
    FileTimeToSystemTime(&ftLocal, &st);
    snprintf(displayString, MAX_TIMESTAMP_LEN, "%u-%.2u-%.2u %.2u:%.2u:%.2u.%.3u", st.wYear,
             st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
}

static bool testWin32EventLog() {
    ELOG_BEGIN_TEST();
    const char* cfg = "sys://eventlog?event_source_name=elog_test&event_id=1234&name=elog_test";
    double msgPerf = 0.0f;
    double ioPerf = 0.0f;
    time_t testStartTime = time(NULL);
    uint32_t testMsgCount = 10;
    runSingleThreadedTest("Win32 Event Log", cfg, msgPerf, ioPerf, TT_NORMAL, testMsgCount);

    // now we need to find the events in the event log
    HANDLE hLog = OpenEventLogA(NULL, "elog_test");
    if (hLog == NULL) {
        ELOG_WIN32_ERROR(OpenEventLogA, "Could not open event log by name 'elog_test");
        return false;
    }

    EVENTLOGRECORD buffer[4096];
    DWORD bytesRead, minBytesNeeded;
    if (!ReadEventLogA(hLog, EVENTLOG_SEQUENTIAL_READ | EVENTLOG_BACKWARDS_READ, 0, &buffer,
                       sizeof(buffer), &bytesRead, &minBytesNeeded)) {
        ELOG_WIN32_ERROR(ReadEventLogA, "Could not read event log by name 'elog_test");
        return false;
    }

    // read recent events backwards and verify test result
    // we expect to see exactly 13 records (due to pre-init 2 log messages, and one test error
    // message at runSingleThreadedTest), which belong to elog_test provider and have a higher
    // timestamp, and we should stop when timestamp goes beyond test start time
    uint32_t matchingRecords = 0;
    PBYTE pRecord = (PBYTE)buffer;
    PBYTE pEndOfRecords = (PBYTE)(buffer + bytesRead);
    while (pRecord < pEndOfRecords) {
        PEVENTLOGRECORD eventRecord = (PEVENTLOGRECORD)pRecord;
        if (eventRecord->TimeGenerated < testStartTime) {
            break;
        }
        char* providerName = (char*)(pRecord + sizeof(EVENTLOGRECORD));
        uint32_t statusCode = eventRecord->EventID & 0xFFFF;
        if ((strcmp(providerName, "elog_test") == 0) && statusCode == 1234) {
            ELOG_DEBUG_EX(sTestLogger, "provider name: %s", providerName);
            ELOG_DEBUG_EX(sTestLogger, "status code: %d", statusCode);
            char timeStamp[MAX_TIMESTAMP_LEN];
            GetTimestamp(eventRecord->TimeGenerated, timeStamp);
            ELOG_DEBUG_EX(sTestLogger, "Time stamp: %s", timeStamp);
            ELOG_DEBUG_EX(sTestLogger, "record number: %lu", eventRecord->RecordNumber);
            ELOG_DEBUG_EX(sTestLogger, "event type: %s",
                          pEventTypeNames[GetEventTypeName(eventRecord->EventType)]);
            char* pMessage = (char*)(pRecord + eventRecord->StringOffset);
            if (pMessage != nullptr) {
                ELOG_DEBUG_EX(sTestLogger, "event first string arg: %s", pMessage);
            }
            ELOG_DEBUG_EX(sTestLogger, "");

            ++matchingRecords;
        }
        pRecord += eventRecord->Length;
    }

    CloseEventLog(hLog);

    // NOTE: we need a filter to avoid counting pre-init messages with log level higher than INFO,
    // otherwise the test fails
    class ELogTestFilter : public elog::ELogFilter {
    public:
        ELogTestFilter() : elog::ELogFilter("test") {}
        bool filterLogRecord(const elog::ELogRecord& logRecord) final {
            return logRecord.m_logLevel <= elog::ELEVEL_INFO;
        }
    };

    ELogTestFilter testFilter;
    uint32_t expectedRecordCount = testMsgCount + elog::getAccumulatedMessageCount(&testFilter);
    if (matchingRecords != expectedRecordCount) {
        ELOG_ERROR_EX(sTestLogger,
                      "Event Log test failed, expecting %u records, but instead found %u",
                      expectedRecordCount, matchingRecords);
        return false;
    }
    ELOG_END_TEST();
}

TEST(ELogCore, Win32EventLog) {
    bool res = testWin32EventLog();
    EXPECT_EQ(res, true);
}
#endif