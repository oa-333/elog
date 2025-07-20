#include "elog_logger.h"

#include "elog_def.h"

#ifndef ELOG_MSVC
#include <sys/time.h>
#include <unistd.h>
#endif  // not defined ELOG_MSVC

#include <cstring>

#include "elog.h"
#include "elog_common.h"
#include "elog_error.h"

#ifndef ELOG_WINDOWS
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace elog {

static thread_local uint64_t sNextRecordId = 0;

void ELogLogger::logFormat(ELogLevel logLevel, const char* file, int line, const char* function,
                           const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    logFormatV(logLevel, file, line, function, fmt, args);
    va_end(args);
}

void ELogLogger::logFormatV(ELogLevel logLevel, const char* file, int line, const char* function,
                            const char* fmt, va_list args) {
    ELogRecordBuilder* recordBuilder = getRecordBuilder();
    if (isLogging(recordBuilder)) {
        recordBuilder = pushRecordBuilder();
    }
    startLogRecord(recordBuilder->getLogRecord(), logLevel, file, line, function);
    appendMsgV(recordBuilder, fmt, args);
    finishLog(recordBuilder);
}

void ELogLogger::logNoFormat(ELogLevel logLevel, const char* file, int line, const char* function,
                             const char* msg) {
    ELogRecordBuilder* recordBuilder = getRecordBuilder();
    if (isLogging(recordBuilder)) {
        recordBuilder = pushRecordBuilder();
    }
    startLogRecord(recordBuilder->getLogRecord(), logLevel, file, line, function);
    appendMsg(recordBuilder, msg);
    finishLog(recordBuilder);
}

void ELogLogger::startLog(ELogLevel logLevel, const char* file, int line, const char* function,
                          const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    ELogRecordBuilder* recordBuilder = getRecordBuilder();
    if (isLogging(recordBuilder)) {
        recordBuilder = pushRecordBuilder();
    }
    startLogRecord(recordBuilder->getLogRecord(), logLevel, file, line, function);
    appendMsgV(recordBuilder, fmt, args);
    va_end(args);
}

void ELogLogger::startLogNoFormat(ELogLevel logLevel, const char* file, int line,
                                  const char* function, const char* msg) {
    ELogRecordBuilder* recordBuilder = getRecordBuilder();
    if (isLogging(recordBuilder)) {
        recordBuilder = pushRecordBuilder();
    }
    startLogRecord(recordBuilder->getLogRecord(), logLevel, file, line, function);
    appendMsg(recordBuilder, msg);
}

void ELogLogger::appendLog(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    ELogRecordBuilder* recordBuilder = getRecordBuilder();
    if (isLogging(recordBuilder)) {
        appendMsgV(recordBuilder, fmt, args);
    } else {
        fprintf(stderr, "Attempt to append log message without start-log being issued first: ");
        vfprintf(stderr, fmt, args);
        fputs("\n", stderr);
        fflush(stderr);
    }
    va_end(args);
}

void ELogLogger::appendLogNoFormat(const char* msg) {
    ELogRecordBuilder* recordBuilder = getRecordBuilder();
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

void ELogLogger::finishLog(ELogRecordBuilder* recordBuilder) {
    if (isLogging(recordBuilder)) {
        // NOTE: new line character at the end of the line is added by each log target individually
        // add terminating null and transfer to log record
        recordBuilder->finalize();

        // send to log targets
        const ELogRecord& logRecord = recordBuilder->getLogRecord();
        if (elog::filterLogMsg(logRecord)) {
            elog::logMsg(logRecord, m_logSource->getLogTargetAffinityMask());
        }

        // reset log record data
        recordBuilder->reset();
        popRecordBuilder();
    } else {
        ELOG_REPORT_ERROR("attempt to end log message without start-log being issued first\n");
    }
}

void ELogLogger::startLogRecord(ELogRecord& logRecord, ELogLevel logLevel, const char* file,
                                int line, const char* function) {
    logRecord.m_logRecordId = sNextRecordId++;
    logRecord.m_logLevel = logLevel;
    logRecord.m_file = file;
    // in case of overflow we report zero
    // TODO: maybe we can take 2 bytes from another filed (e.g. not so importnat record id)?
    logRecord.m_line = line > UINT16_MAX ? (uint16_t)0 : (uint16_t)line;
    logRecord.m_function = function;
    elogGetCurrentTime(logRecord.m_logTime);
    logRecord.m_threadId = getCurrentThreadId();
    logRecord.m_logger = this;
}

}  // namespace elog
