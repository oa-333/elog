#include "elog_logger.h"

#include <sys/time.h>
#include <unistd.h>

#include <cstdarg>
#include <cstring>

#include "elog_system.h"

#ifdef __WIN32__
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
#ifdef SYS_gettid
#define gettid() syscall(SYS_gettid)
#else
#error "SYS_gettid unavailable on this system"
#endif
#endif

namespace elog {

thread_local std::vector<char> ELogLogger::sMsgBuffer;
thread_local uint32_t ELogLogger::sMsgBufferOffset;
thread_local ELogRecord ELogLogger::sLogRecord;
std::atomic<uint64_t> ELogLogger::sNextRecordId = 0;

static uint64_t getCurrentThreadId() {
#ifdef __WIN32__
    return GetCurrentThreadId();
#else
    return gettid();
#endif
}

static uint32_t elog_strncpy(char* dest, const char* src, uint32_t dest_len) {
    uint32_t src_len = strlen(src);
    if (src_len + 1 < dest_len) {
        // copy terminating null as well
        strncpy(dest, src, src_len + 1);
        return src_len;
    }
    // reserve one char for terminating null
    int copy_len = dest_len - 1;
    strncpy(dest, src, copy_len);

    // add terminating null
    dest[copy_len] = 0;

    // return number of bytes copied, excluding terminating null
    return copy_len;
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
        // add terminating null and transfer to log record
        appendMsg("\n");
        sLogRecord.m_logMsg = (const char*)&sMsgBuffer[0];

        // send to log targets
        ELogSystem::log(sLogRecord);

        // reset log buffer
        sMsgBufferOffset = 0;
    } else {
        fprintf(stderr, "attempt to end log message without start-log being issued first\n");
    }
}

void ELogLogger::startLogRecord(ELogLevel logLevel) {
    sLogRecord.m_logRecordId = sNextRecordId.fetch_add(1, std::memory_order_relaxed);
    sLogRecord.m_logLevel = logLevel;
    gettimeofday(&sLogRecord.m_logTime, NULL);
    sLogRecord.m_threadId = getCurrentThreadId();
    sLogRecord.m_sourceId = m_logSource->getId();
}

void ELogLogger::appendMsgV(const char* fmt, va_list ap) {
    va_list apCopy;
    va_copy(apCopy, ap);
    uint32_t requiredBytes = (vsnprintf(nullptr, 0, fmt, apCopy) + 1);
    ensureBufferLength(requiredBytes);
    sMsgBufferOffset +=
        vsnprintf(&sMsgBuffer[sMsgBufferOffset], sMsgBuffer.size() - sMsgBufferOffset, fmt, ap);
}

void ELogLogger::appendMsg(const char* msg) {
    uint32_t requiredBytes = (strlen(msg) + 1);
    ensureBufferLength(requiredBytes);
    sMsgBufferOffset += elog_strncpy(&sMsgBuffer[sMsgBufferOffset], msg, requiredBytes);
}

}  // namespace elog
