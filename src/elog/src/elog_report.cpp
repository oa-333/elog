#include "elog_report.h"

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "elog.h"
#include "elog_common.h"
#include "elog_common_def.h"

#ifdef ELOG_MINGW
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

namespace elog {

static thread_local bool sIsReporting = false;

class ELogDefaultReportHandler : public ELogReportHandler {
public:
    ELogDefaultReportHandler() {}
    ELogDefaultReportHandler(const ELogDefaultReportHandler&) = delete;
    ELogDefaultReportHandler(ELogDefaultReportHandler&&) = delete;
    ELogDefaultReportHandler& operator=(const ELogDefaultReportHandler&) = delete;
    ~ELogDefaultReportHandler() final {}

    void onReportV(ELogLevel logLevel, const char* file, int line, const char* function,
                   const char* fmt, va_list args) override {
        // TODO: this may get scattered if other threads are also writing to stderr
        fprintf(stderr, "<ELOG> %s: ", elogLevelToStr(logLevel));
        vfprintf(stderr, fmt, args);
        fprintf(stderr, "\n");
        if (logLevel <= ELEVEL_ERROR) {
            fprintf(stderr, "Error location: file: %s, line: %d, function: %s\n", file, line,
                    function);
        }
        fflush(stderr);
    }

    void onReport(ELogLevel logLevel, const char* file, int line, const char* function,
                  const char* msg) override {
        // TODO: this may get scattered if other threads are also writing to stderr
        fprintf(stderr, "<ELOG> %s: %s\n", elogLevelToStr(logLevel), msg);
        fprintf(stderr, "Error location: file: %s, line: %d, function: %s\n", file, line, function);
        fflush(stderr);
    }
};

class ELogSelfReportHandler : public ELogReportHandler {
public:
    ELogSelfReportHandler() : m_logger(nullptr) {}
    ELogSelfReportHandler(const ELogSelfReportHandler&) = delete;
    ELogSelfReportHandler(ELogSelfReportHandler&&) = delete;
    ELogSelfReportHandler& operator=(const ELogSelfReportHandler&) = delete;
    ~ELogSelfReportHandler() final {}

    void initialize() {
        // at this point we can create a logger and restrict to stderr
        m_logger = elog::getSharedLogger("elog");
        restrictToStdErr();
    }

    inline ELogLogger* getLogger() { return m_logger; }

    void onReportV(ELogLevel logLevel, const char* file, int line, const char* function,
                   const char* fmt, va_list args) override {
        m_logger->logFormatV(logLevel, file, line, function, fmt, args);
    }

    void onReport(ELogLevel logLevel, const char* file, int line, const char* function,
                  const char* msg) override {
        m_logger->logFormat(logLevel, file, line, function, msg);
    }

private:
    ELogLogger* m_logger;

    inline bool restrictToStdErr() {
        // we create a dedicated stderr target with colored formatting
        // NOTE: we disable statistics collection for this target, because it generates circular
        // reporting during statistics slot allocation (due to trace reports in elog_stats.cpp)
        // anyway we don't need statistics here
        const char* cfg =
            "sys://stderr?name=elog_internal&"
            "log_format=${fmt:begin-font=faint}"
            "${time} "
            "${switch: ${level}:"
            "   ${case: ${const-level: WARN}: ${fmt:begin-fg-color=yellow}} :"
            "   ${case: ${const-level: ERROR}: ${fmt:begin-fg-color=red}} :"
            "   ${case: ${const-level: FATAL}: ${fmt:begin-fg-color=red}}}"
            "${level:6}${fmt:default} "
            "${fmt:begin-font=faint}"
            "[${tid}] ${src:font=underline} "
            "${fmt:begin-font=faint}"
            "${msg}"
            "${fmt:default}&"
            "enable_stats=no";
        ELogTargetId logTargetId = elog::configureLogTarget(cfg);
        if (logTargetId == ELOG_INVALID_TARGET_ID) {
            ELOG_REPORT_ERROR("Failed to configure log target for ELog reports");
            return false;
        }

        // get the log target
        ELogTarget* logTarget = elog::getLogTarget(logTargetId);
        if (logTarget == nullptr) {
            ELOG_REPORT_ERROR("Could not find ELog reports target by id %u", logTargetId);
            return false;
        }

        // bind the logger to this specific target
        ELogTargetAffinityMask mask = 0;
        ELOG_ADD_TARGET_AFFINITY_MASK(mask, logTargetId);
        m_logger->getLogSource()->setLogTargetAffinity(mask);

        // make sure no one else sends to this target
        logTarget->setPassKey();
        m_logger->getLogSource()->addPassKey(logTarget->getPassKey());

        // make sure no one pulls the rug under our feet (e.g. through clearAllLogTargets())
        logTarget->setSystemTarget();
        return true;
    }
};

static ELogDefaultReportHandler sDefaultReportHandler;
static ELogSelfReportHandler sSelfReportHandler;
static ELogReportHandler* sReportHandler = &sDefaultReportHandler;
static ELogLevel sReportLevel = ELEVEL_WARN;

void ELogReport::setReportHandler(ELogReportHandler* reportHandler) {
    if (reportHandler != nullptr) {
        sReportHandler = reportHandler;
    } else {
        sReportHandler = &sDefaultReportHandler;
    }
    sReportHandler->setReportLevel(sReportLevel);
}

ELogReportHandler* ELogReport::getReportHandler() { return sReportHandler; }

void ELogReport::setReportLevel(ELogLevel reportLevel) { sReportLevel = reportLevel; }

ELogLevel ELogReport::getReportLevel() { return sReportLevel; }

void ELogReport::report(ELogLevel logLevel, const char* file, int line, const char* function,
                        const char* fmt, ...) {
    if (logLevel <= sReportLevel) {
        va_list args;
        va_start(args, fmt);

        if (sIsReporting) {
            sDefaultReportHandler.onReportV(logLevel, file, line, function, fmt, args);
        } else {
            sIsReporting = true;
            ELogReportHandler* reportHandler =
                sReportHandler ? sReportHandler : &sDefaultReportHandler;
            reportHandler->onReportV(logLevel, file, line, function, fmt, args);
            sIsReporting = false;
        }

        va_end(args);
    }
}

char* ELogReport::sysErrorToStr(int sysErrorCode) {
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
char* ELogReport::win32SysErrorToStr(unsigned long sysErrorCode) {
    LPSTR messageBuffer = nullptr;
    FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, sysErrorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0,
        nullptr);
    return messageBuffer;
}

void ELogReport::win32FreeErrorStr(char* errStr) { LocalFree(errStr); }
#endif

void ELogReport::initReport() {
    sSelfReportHandler.initialize();
    setReportHandler(&sSelfReportHandler);
}

void ELogReport::termReport() { setReportHandler(&sDefaultReportHandler); }

}  // namespace elog
