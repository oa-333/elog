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
    ELogRecordBuilder& recordBuilder = getRecordBuilder();
    if (isLogging(recordBuilder)) {
        pushRecordBuilder();
        recordBuilder = getRecordBuilder();
    }
    startLogRecord(recordBuilder.getLogRecord(), logLevel, file, line, function);
    appendMsgV(recordBuilder, fmt, args);
    finishLog(recordBuilder);
}

void ELogLogger::logNoFormat(ELogLevel logLevel, const char* file, int line, const char* function,
                             const char* msg) {
    ELogRecordBuilder& recordBuilder = getRecordBuilder();
    if (isLogging(recordBuilder)) {
        pushRecordBuilder();
        recordBuilder = getRecordBuilder();
    }
    startLogRecord(recordBuilder.getLogRecord(), logLevel, file, line, function);
    appendMsg(recordBuilder, msg);
    finishLog(recordBuilder);
}

void ELogLogger::startLog(ELogLevel logLevel, const char* file, int line, const char* function,
                          const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    ELogRecordBuilder& recordBuilder = getRecordBuilder();
    if (isLogging(recordBuilder)) {
        pushRecordBuilder();
        recordBuilder = getRecordBuilder();
    }
    startLogRecord(recordBuilder.getLogRecord(), logLevel, file, line, function);
    appendMsgV(recordBuilder, fmt, ap);
    va_end(ap);
}

void ELogLogger::startLogNoFormat(ELogLevel logLevel, const char* file, int line,
                                  const char* function, const char* msg) {
    ELogRecordBuilder& recordBuilder = getRecordBuilder();
    if (isLogging(recordBuilder)) {
        pushRecordBuilder();
        recordBuilder = getRecordBuilder();
    }
    startLogRecord(recordBuilder.getLogRecord(), logLevel, file, line, function);
    appendMsg(recordBuilder, msg);
}

void ELogLogger::appendLog(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    ELogRecordBuilder& recordBuilder = getRecordBuilder();
    if (isLogging(recordBuilder)) {
        appendMsgV(recordBuilder, fmt, ap);
    } else {
        fprintf(stderr, "Attempt to append log message without start-log being issued first: ");
        vfprintf(stderr, fmt, ap);
        fputs("\n", stderr);
        fflush(stderr);
    }
    va_end(ap);
}

void ELogLogger::appendLogNoFormat(const char* msg) {
    ELogRecordBuilder& recordBuilder = getRecordBuilder();
    if (isLogging(recordBuilder)) {
        appendMsg(recordBuilder, msg);
    } else {
        fprintf(stderr,
                "Attempt to append unformatted log message without start-log being issued first: ");
        fputs(msg, stderr);
        fputs("\n", stderr);
        fflush(stderr);
    }
}

void ELogLogger::finishLog(ELogRecordBuilder& recordBuilder) {
    if (isLogging(recordBuilder)) {
        // NOTE: new line character at the end of the line is added by each log target individually
        // add terminating null and transfer to log record
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

void ELogLogger::startLogRecord(ELogRecord& logRecord, ELogLevel logLevel, const char* file,
                                int line, const char* function) {
    logRecord.m_logRecordId = sNextRecordId.fetch_add(1, std::memory_order_relaxed);
    logRecord.m_logLevel = logLevel;
    logRecord.m_file = file;
    logRecord.m_line = line;
    logRecord.m_function = function;
    getCurrentTime(logRecord.m_logTime);
    logRecord.m_threadId = getCurrentThreadId();
    logRecord.m_logger = this;
}

}  // namespace elog
