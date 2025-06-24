#include "elog_logger.h"

#include "elog_def.h"

#ifndef ELOG_MSVC
#include <sys/time.h>
#include <unistd.h>
#endif  // not defined ELOG_MSVC

#include <cstring>

#include "elog_common.h"
#include "elog_error.h"
#include "elog_system.h"

#ifndef ELOG_WINDOWS
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace elog {

static std::atomic<uint64_t> sNextRecordId = 0;

void ELogLogger::logFormat(ELogLevel logLevel, const char* file, int line, const char* function,
                           const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    logFormatV(logLevel, file, line, function, fmt, ap);
    va_end(ap);
}

void ELogLogger::logFormatV(ELogLevel logLevel, const char* file, int line, const char* function,
                            const char* fmt, va_list args) {
    if (isLogging()) {
        pushRecordBuilder();
    }
    startLogRecord(logLevel, file, line, function);
    appendMsgV(fmt, args);
    finishLog();
}

void ELogLogger::logNoFormat(ELogLevel logLevel, const char* file, int line, const char* function,
                             const char* msg) {
    if (isLogging()) {
        pushRecordBuilder();
    }
    startLogRecord(logLevel, file, line, function);
    appendMsg(msg);
    finishLog();
}

void ELogLogger::startLog(ELogLevel logLevel, const char* file, int line, const char* function,
                          const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    if (isLogging()) {
        pushRecordBuilder();
    }
    startLogRecord(logLevel, file, line, function);
    appendMsgV(fmt, ap);
    va_end(ap);
}

void ELogLogger::startLogNoFormat(ELogLevel logLevel, const char* file, int line,
                                  const char* function, const char* msg) {
    if (isLogging()) {
        pushRecordBuilder();
    }
    startLogRecord(logLevel, file, line, function);
    appendMsg(msg);
}

void ELogLogger::appendLog(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    if (isLogging()) {
        appendMsgV(fmt, ap);
    } else {
        fprintf(stderr, "Attempt to append log message without start-log being issued first: ");
        vfprintf(stderr, fmt, ap);
        fputs("\n", stderr);
        fflush(stderr);
    }
    va_end(ap);
}

void ELogLogger::appendLogNoFormat(const char* msg) {
    if (isLogging()) {
        appendMsg(msg);
    } else {
        fprintf(stderr,
                "Attempt to append unformatted log message without start-log being issued first: ");
        fputs(msg, stderr);
        fputs("\n", stderr);
        fflush(stderr);
    }
}

void ELogLogger::finishLog() {
    if (isLogging()) {
        // NOTE: new line character at the end of the line is added by each log target individually
        // add terminating null and transfer to log record
        ELogRecordBuilder& recordBuilder = getRecordBuilder();
        recordBuilder.finalize();

        // send to log targets
        const ELogRecord& logRecord = recordBuilder.getLogRecord();
        if (ELogSystem::filterLogMsg(logRecord)) {
            ELogSystem::log(logRecord, m_logSource->getLogTargetAffinityMask());
        }

        // reset log record data
        recordBuilder.reset();
        popRecordBuilder();
    } else {
        ELOG_REPORT_ERROR("attempt to end log message without start-log being issued first\n");
    }
}

void ELogLogger::startLogRecord(ELogLevel logLevel, const char* file, int line,
                                const char* function) {
    ELogRecord& logRecord = getRecordBuilder().getLogRecord();
    logRecord.m_logRecordId = sNextRecordId.fetch_add(1, std::memory_order_relaxed);
    logRecord.m_logLevel = logLevel;
    logRecord.m_file = file;
    logRecord.m_line = line;
    logRecord.m_function = function;
#ifdef ELOG_TIME_USE_CHRONO
    logRecord.m_logTime = std::chrono::system_clock::now();
#else
#ifdef ELOG_MSVC
#ifdef ELOG_TIME_USE_SYSTEMTIME
    GetLocalTime(&logRecord.m_logTime);
#else
    GetSystemTimeAsFileTime(&logRecord.m_logTime);
#endif
#else
    // NOTE: gettimeofday is obsolete, instead clock_gettime() should be used
    clock_gettime(CLOCK_REALTIME, &logRecord.m_logTime);
#endif
#endif
    logRecord.m_threadId = getCurrentThreadId();
    logRecord.m_logger = this;
}

void ELogLogger::appendMsgV(const char* fmt, va_list ap) {
    va_list apCopy;
    va_copy(apCopy, ap);
    uint32_t requiredBytes = (vsnprintf(nullptr, 0, fmt, apCopy) + 1);
    ELogRecordBuilder& recordBuilder = getRecordBuilder();
    if (recordBuilder.ensureBufferLength(requiredBytes)) {
        recordBuilder.appendV(fmt, ap);
    }
    va_end(apCopy);
}

void ELogLogger::appendMsg(const char* msg) {
    uint32_t requiredBytes = (strlen(msg) + 1);
    ELogRecordBuilder& recordBuilder = getRecordBuilder();
    if (recordBuilder.ensureBufferLength(requiredBytes)) {
        recordBuilder.append(msg);
    }
}

}  // namespace elog
