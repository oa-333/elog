#include "elog_logger.h"

#include "elog_def.h"

#ifndef ELOG_MSVC
#include <sys/time.h>
#include <unistd.h>
#endif  // not defined ELOG_MSVC

#include <cstring>

#include "elog.h"
#include "elog_common.h"
#include "elog_internal.h"
#include "elog_report.h"

#ifndef ELOG_WINDOWS
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

// reduce noise coming from fmt lib
#ifdef ELOG_MSVC
#pragma warning(push)
#pragma warning(disable : 4582 4623 4625 4626 5027 5026)
#endif

#ifdef ELOG_ENABLE_FMT_LIB
#include <fmt/args.h>
#include <fmt/core.h>

#include "elog_cache.h"
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
                                int line, const char* function,
                                uint8_t flags /* = ELOG_RECORD_FORMATTED */) {
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
    logRecord.m_flags = flags;
}

#ifdef ELOG_ENABLE_FMT_LIB
bool ELogLogger::resolveLogRecord(const ELogRecord& logRecord, ELogBuffer& logBuffer) {
    // prepare parameter array (use implicit alloca by compiler)
    const char* bufPtr = logRecord.m_logMsg;
    uint8_t paramCount = *(uint8_t*)bufPtr;
    size_t offset = 1;

    // extract format string
    const char* fmtStr = nullptr;
    if (logRecord.m_flags & ELOG_RECORD_FMT_CACHED) {
        ELogCacheEntryId cacheEntryId = *(ELogCacheEntryId*)(bufPtr + offset);
        fmtStr = ELogCache::getCachedFormatMsg(cacheEntryId);
        offset += sizeof(ELogCacheEntryId);
    } else {
        fmtStr = bufPtr + offset;
        offset += strlen(fmtStr) + 1;
    }

    // prepare argument list for fmtlib
    fmt::dynamic_format_arg_store<fmt::format_context> store;

#define ELOG_COLLECT_ARG(argType)                        \
    {                                                    \
        argType argValue = *(argType*)(bufPtr + offset); \
        store.push_back(argValue);                       \
        offset += sizeof(argType);                       \
        break;                                           \
    }

    for (uint8_t i = 0; i < paramCount; ++i) {
        uint8_t code = *(uint8_t*)(bufPtr + offset);
        ++offset;
        switch (code) {
            case ELOG_UINT8_CODE:
                ELOG_COLLECT_ARG(uint8_t);

            case ELOG_UINT16_CODE:
                ELOG_COLLECT_ARG(uint16_t);

            case ELOG_UINT32_CODE:
                ELOG_COLLECT_ARG(uint32_t);

            case ELOG_UINT64_CODE:
                ELOG_COLLECT_ARG(uint64_t);

            case ELOG_INT8_CODE:
                ELOG_COLLECT_ARG(int8_t);

            case ELOG_INT16_CODE:
                ELOG_COLLECT_ARG(int16_t);

            case ELOG_INT32_CODE:
                ELOG_COLLECT_ARG(int32_t);

            case ELOG_INT64_CODE:
                ELOG_COLLECT_ARG(int64_t);

            case ELOG_FLOAT_CODE:
                ELOG_COLLECT_ARG(float);

            case ELOG_DOUBLE_CODE:
                ELOG_COLLECT_ARG(double);

            case ELOG_BOOL_CODE:
                ELOG_COLLECT_ARG(bool);

            case ELOG_STRING_CODE: {
                const char* arg = (const char*)(bufPtr + offset);
                store.push_back(arg);
                offset += (strlen(arg) + 1);  // skip over terminating null
                break;
            }

            default:
                ELOG_REPORT_ERROR("Invalid argument type code %u while resolving binary log record",
                                  (unsigned)code);
                return false;
        }
    }

    // now we can format
    // TODO: use vformat_to and try to avoid malloc/free, but we must have a safe iterator over the
    // log buffer, such that if size limit is breached it will realloc
    // currently it is no possible to do so with libfmt. we can optimize here with our own allocator
    // maybe even use alloca to do faster than malloc/free
    auto text = fmt::vformat(fmtStr, store);
    logBuffer.append(text.c_str(), text.length());
    return true;
}
#endif

}  // namespace elog

#ifdef ELOG_MSVC
#pragma warning(pop)
#endif