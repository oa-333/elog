#include "elog_logger.h"

#include "elog_def.h"

#ifndef ELOG_MSVC
#include <sys/time.h>
#include <unistd.h>
#endif  // not defined ELOG_MSVC

#include <cstdarg>
#include <cstring>

#include "elog_system.h"

#ifdef ELOG_MINGW
// we need windows headers for MinGW
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif !defined(ELOG_WINDOWS)
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
#ifdef SYS_gettid
#define gettid() syscall(SYS_gettid)
#else
#error "SYS_gettid unavailable on this platform"
#endif
#endif

namespace elog {

static std::atomic<uint64_t> sNextRecordId = 0;

static uint64_t getCurrentThreadId() {
#ifdef ELOG_WINDOWS
    return GetCurrentThreadId();
#else
    return gettid();
#endif  // ELOG_WINDOWS
}

void ELogLogger::logFormat(ELogLevel logLevel, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    if (!isLogging()) {
        startLogRecord(logLevel);
        appendMsgV(fmt, ap);
        finishLog();
    } else {
        fprintf(stderr,
                "Attempt to log message while previous start-log call has not finished yet: ");
        vfprintf(stderr, fmt, ap);
        fputs("\n", stderr);
        fflush(stderr);
    }
    va_end(ap);
}

void ELogLogger::logNoFormat(ELogLevel logLevel, const char* msg) {
    if (!isLogging()) {
        startLogRecord(logLevel);
        appendMsg(msg);
        finishLog();
    } else {
        fprintf(stderr,
                "Attempt to log unformatted message while previous start-log call has not finished "
                "yet: ");
        fputs(msg, stderr);
        fputs("\n", stderr);
        fflush(stderr);
    }
}

void ELogLogger::startLog(ELogLevel logLevel, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    if (!isLogging()) {
        startLogRecord(logLevel);
        appendMsgV(fmt, ap);
    } else {
        fprintf(
            stderr,
            "Attempt to start log message while previous start-log call has not finished yet: ");
        vfprintf(stderr, fmt, ap);
        fputs("\n", stderr);
        fflush(stderr);
    }
    va_end(ap);
}

void ELogLogger::startLogNoFormat(ELogLevel logLevel, const char* msg) {
    if (!isLogging()) {
        startLogRecord(logLevel);
        appendMsg(msg);
    } else {
        fprintf(stderr,
                "Attempt to start log unformatted message while previous start-log call has not "
                "finished yet: ");
        fputs(msg, stderr);
        fputs("\n", stderr);
        fflush(stderr);
    }
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
        ELogSystem::log(recordBuilder.getLogRecord());

        // reset log record data
        recordBuilder.reset();
    } else {
        ELogSystem::reportError(
            "attempt to end log message without start-log being issued first\n");
    }
}

bool ELogLogger::isLogging() const { return getRecordBuilder().getOffset() > 0; }

void ELogLogger::startLogRecord(ELogLevel logLevel) {
    ELogRecord& logRecord = getRecordBuilder().getLogRecord();
    logRecord.m_logRecordId = sNextRecordId.fetch_add(1, std::memory_order_relaxed);
    logRecord.m_logLevel = logLevel;
#ifdef ELOG_MSVC
    ::GetSystemTime(&logRecord.m_logTime);
#else
    gettimeofday(&logRecord.m_logTime, NULL);
#endif
    logRecord.m_threadId = getCurrentThreadId();
    logRecord.m_sourceId = m_logSource->getId();
}

void ELogLogger::appendMsgV(const char* fmt, va_list ap) {
    va_list apCopy;
    va_copy(apCopy, ap);
    uint32_t requiredBytes = (vsnprintf(nullptr, 0, fmt, apCopy) + 1);
    ELogRecordBuilder& recordBuilder = getRecordBuilder();
    if (recordBuilder.ensureBufferLength(requiredBytes)) {
        recordBuilder.appendV(fmt, ap);
    }
}

void ELogLogger::appendMsg(const char* msg) {
    uint32_t requiredBytes = (strlen(msg) + 1);
    ELogRecordBuilder& recordBuilder = getRecordBuilder();
    if (recordBuilder.ensureBufferLength(requiredBytes)) {
        recordBuilder.append(msg);
    }
}

}  // namespace elog
