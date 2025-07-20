#include "elog_error.h"

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "elog.h"
#include "elog_common.h"

#ifdef ELOG_MINGW
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace elog {

static std::atomic<ELogTargetId> sStderrTargetId = ELOG_INVALID_TARGET_ID;

class ELogDefaultErrorHandler : public ELogErrorHandler {
public:
    ELogDefaultErrorHandler() {}
    ELogDefaultErrorHandler(const ELogDefaultErrorHandler&) = delete;
    ELogDefaultErrorHandler(ELogDefaultErrorHandler&&) = delete;
    ELogDefaultErrorHandler& operator=(const ELogDefaultErrorHandler&) = delete;
    ~ELogDefaultErrorHandler() final {}

    void onError(const char* msg) final {
        fprintf(stderr, "<ELOG> ERROR: %s\n", msg);
        fflush(stderr);
    }

    void onWarn(const char* msg) final {
        fprintf(stderr, "<ELOG> WARN: %s\n", msg);
        fflush(stderr);
    }

    void onTrace(const char* msg) final {
        fprintf(stderr, "<ELOG> TRACE: %s\n", msg);
        fflush(stderr);
    }
};

class ELogSelfErrorHandler : public ELogErrorHandler {
public:
    ELogSelfErrorHandler() : m_logSource(nullptr), m_logger(nullptr) {}
    ELogSelfErrorHandler(const ELogSelfErrorHandler&) = delete;
    ELogSelfErrorHandler(ELogSelfErrorHandler&&) = delete;
    ELogSelfErrorHandler& operator=(const ELogSelfErrorHandler&) = delete;
    ~ELogSelfErrorHandler() final {}

    void init() {
        m_logSource = elog::defineLogSource("elog");
        if (m_logSource != nullptr) {
            m_logger = m_logSource->createSharedLogger();
        }
        // NOTE: it is too soon to restrict this logger to stderr, so we postpone it to a later
        // phase
    }

    void setTraceMode(bool enableTrace) final {
        m_logSource->setLogLevel(enableTrace ? ELEVEL_TRACE : ELEVEL_INFO,
                                 ELogPropagateMode::PM_SET);
        ELogErrorHandler::setTraceMode(enableTrace);
    }

    void onError(const char* msg) final {
        if (restrictToStdErr()) {
            ELOG_ERROR_EX(m_logger, msg);
        }
    }

    void onWarn(const char* msg) final {
        if (restrictToStdErr()) {
            ELOG_WARN_EX(m_logger, msg);
        }
    }

    void onTrace(const char* msg) final {
        if (restrictToStdErr()) {
            ELOG_TRACE_EX(m_logger, msg);
        }
    }

private:
    ELogSource* m_logSource;
    ELogLogger* m_logger;

    inline bool restrictToStdErr() {
        ELogTargetId logTargetId = sStderrTargetId.load(std::memory_order_relaxed);
        if (logTargetId == ELOG_INVALID_TARGET_ID) {
            ELogTargetId stderrTargetId = elog::getLogTargetId("stderr");
            if (stderrTargetId != ELOG_INVALID_TARGET_ID) {
                if (sStderrTargetId.compare_exchange_strong(logTargetId, stderrTargetId,
                                                            std::memory_order_seq_cst)) {
                    ELogTargetAffinityMask mask = 0;
                    ELOG_ADD_TARGET_AFFINITY_MASK(mask, stderrTargetId);
                    m_logSource->setLogTargetAffinity(mask);
                    logTargetId = stderrTargetId;
                }
                // if we failed to CAS it is possible that that the thread that made CAS will
                // experience context switch before actually restricting the log source, so this
                // thread must wait for next round
            }
        }
        return (logTargetId != ELOG_INVALID_TARGET_ID);
    }
};

static ELogDefaultErrorHandler sDefaultErrorHandler;
static ELogSelfErrorHandler sSelfErrorHandler;
static ELogErrorHandler* sErrorHandler = &sDefaultErrorHandler;

void ELogError::setErrorHandler(ELogErrorHandler* errorHandler) {
    if (errorHandler != nullptr) {
        sErrorHandler = errorHandler;
    } else {
        sErrorHandler = &sDefaultErrorHandler;
    }
}

ELogErrorHandler* ELogError::getErrorHandler() { return sErrorHandler; }

void ELogError::setTraceMode(bool enableTrace) { sErrorHandler->setTraceMode(enableTrace); }

bool ELogError::isTraceEnabled() { return sErrorHandler->isTraceEnabled(); }

void ELogError::reportError(const char* errorMsgFmt, ...) {
    va_list args;
    va_start(args, errorMsgFmt);
    reportV(RT_ERROR, errorMsgFmt, args);
    va_end(args);
}

void ELogError::reportSysError(const char* sysCall, const char* errorMsgFmt, ...) {
    int errCode = errno;
    ELOG_REPORT_ERROR("System call %s() failed: %d (%s)", sysCall, errCode, sysErrorToStr(errCode));

    va_list args;
    va_start(args, errorMsgFmt);
    ELOG_REPORT_ERROR(errorMsgFmt, args);
    va_end(args);
}

void ELogError::reportSysErrorCode(const char* sysCall, int errCode, const char* errorMsgFmt, ...) {
    ELOG_REPORT_ERROR("System call %s() failed: %d (%s)", sysCall, errCode, sysErrorToStr(errCode));

    va_list args;
    va_start(args, errorMsgFmt);
    ELOG_REPORT_ERROR(errorMsgFmt, args);
    va_end(args);
}

void ELogError::reportWarn(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    reportV(RT_WARN, fmt, args);
    va_end(args);
}

void ELogError::reportTrace(const char* fmt, ...) {
    // TODO: allocating a buffer each time is not such a good idea, at least we can use thread local
    // fixed buffer
    static thread_local bool isTracing = false;
    if (sErrorHandler->isTraceEnabled() && !isTracing) {
        isTracing = true;
        va_list args;
        va_start(args, fmt);

        // check how many bytes are required
        va_list argsCopy;
        va_copy(argsCopy, args);
        int res = vsnprintf(nullptr, 0, fmt, argsCopy);
        if (res < 0) {
            // output error occurred, report this, this is highly unexpected
            ELOG_REPORT_ERROR("Failed to format trace buffer");
            return;
        }

        // now we can safely make the cast
        uint32_t requiredBytes = ((uint32_t)res) + 1;

        // format trace message
        char* traceMsg = (char*)malloc(requiredBytes);
        vsnprintf(traceMsg, requiredBytes, fmt, args);

        // report error
        sErrorHandler->onTrace(traceMsg);
        free(traceMsg);
        va_end(argsCopy);

        va_end(args);
        isTracing = false;
    }
}

char* ELogError::sysErrorToStr(int sysErrorCode) {
    const int BUF_LEN = 256;
    static thread_local char buf[BUF_LEN];
#ifdef ELOG_WINDOWS
    (void)strerror_s(buf, BUF_LEN, sysErrorCode);
    return buf;
#else
#if (_POSIX_C_SOURCE >= 200112L) && !_GNU_SOURCE
    (void)strerror_r(sysErrorCode, buf, BUF_LEN);
    return buf;
#else
    return strerror_r(sysErrorCode, buf, BUF_LEN);
#endif
#endif
}

#ifdef ELOG_WINDOWS
char* ELogError::win32SysErrorToStr(unsigned long sysErrorCode) {
    LPSTR messageBuffer = nullptr;
    FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, sysErrorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0,
        NULL);
    return messageBuffer;
}

void ELogError::win32FreeErrorStr(char* errStr) { LocalFree(errStr); }
#endif

void ELogError::initError() {
    std::string elogSink;
    if (elog_getenv("ELOG_SINK", elogSink)) {
        ELOG_REPORT_TRACE("Setting Log sink: %s", elogSink.c_str());
        if (elogSink.compare("logger") == 0) {
            sSelfErrorHandler.init();
            setErrorHandler(&sSelfErrorHandler);
        }
    }

    std::string elogTrace;
    if (elog_getenv("ELOG_TRACE", elogTrace)) {
        if (elogTrace.compare("TRUE") == 0) {
            setTraceMode(true);
        }
    }
}

void ELogError::reportV(ReportType reportType, const char* msgFmt, va_list args) {
    // compute error message length, this requires copying variadic argument pointer
    va_list argsCopy;
    va_copy(argsCopy, args);
    int res = vsnprintf(nullptr, 0, msgFmt, argsCopy);
    if (res < 0) {
        // output error occurred, report this, this is highly unexpected
        ELOG_REPORT_ERROR("Failed to format trace buffer");
        return;
    }

    // now we can safely make the cast
    uint32_t requiredBytes = ((uint32_t)res) + 1;

    // format error message
    char* formattedMsg = (char*)malloc(requiredBytes);
    vsnprintf(formattedMsg, requiredBytes, msgFmt, args);

    // report error
    ELogErrorHandler* errorHandler = sErrorHandler ? sErrorHandler : &sDefaultErrorHandler;
    switch (reportType) {
        case RT_ERROR:
            errorHandler->onError(formattedMsg);
            break;

        case RT_WARN:
            errorHandler->onWarn(formattedMsg);
            break;

        case RT_TRACE:
            errorHandler->onTrace(formattedMsg);
            break;

        default:
            assert(false);
            break;
    }

    free(formattedMsg);
    va_end(argsCopy);
}

}  // namespace elog
