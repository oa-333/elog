#include "elog_report.h"

#include <atomic>
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "elog_api.h"
#include "elog_common.h"
#include "elog_common_def.h"
#include "elog_field_selector_internal.h"

#ifdef ELOG_MINGW
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogReport)

static thread_local uint64_t sDisableReportCount = 0;

static thread_local uint64_t sDefaultReportCount = 0;

bool ELogReportLogger::initialize() {
    if (m_logger == nullptr) {
        std::string qualifiedName = std::string("elog.") + m_name;

        // prevent displaying error message if log source not found or cannot be defined
        ELOG_SCOPED_DISABLE_REPORT();
        ELogSource* logSource = getLogSource(qualifiedName.c_str());

        // define missing log source
        if (logSource == nullptr) {
            logSource = defineLogSource(qualifiedName.c_str());
            if (logSource == nullptr) {
                return false;
            }
        }
        m_logger = logSource->createSharedLogger();
    }
    return true;
}

ELogLogger* ELogReportLogger::getLogger() {
    (void)initialize();
    return m_logger;
}

bool ELogReportLogger::startInit() {
    InitState initState = m_initState.load(std::memory_order_acquire);
    if (initState != InitState::IS_NO_INIT) {
        return false;
    }
    return m_initState.compare_exchange_strong(initState, InitState::IS_DURING_INIT,
                                               std::memory_order_seq_cst);
}

void ELogReportLogger::waitFinishInit() const {
    InitState initState = m_initState.load(std::memory_order_acquire);
    assert(initState != InitState::IS_NO_INIT);
    while (initState != InitState::IS_INIT) {
        std::this_thread::sleep_for(std::chrono::milliseconds(0));
        initState = m_initState.load(std::memory_order_acquire);
    }
}

static thread_local bool sIsReporting = false;

class ELogDefaultReportHandler : public ELogReportHandler {
public:
    ELogDefaultReportHandler() {}
    ELogDefaultReportHandler(const ELogDefaultReportHandler&) = delete;
    ELogDefaultReportHandler(ELogDefaultReportHandler&&) = delete;
    ELogDefaultReportHandler& operator=(const ELogDefaultReportHandler&) = delete;
    ~ELogDefaultReportHandler() final {}

    void onReportV(const ELogReportLogger& reportLogger, ELogLevel logLevel, const char* file,
                   int line, const char* function, const char* fmt, va_list args) override {
        // special case: logging before elog has initialized or after it has terminated
        if (!elog::isInitialized()) {
            // NOTE: a log buffer is used for formatting in order to emit full message in one call
            // and avoid intermixing messages from several threads
            ELogBuffer buffer;
            buffer.appendArgs("<ELOG> %s: ", elogLevelToStr(logLevel));
            buffer.appendV(fmt, args);
            buffer.append(1, '\n');
            buffer.finalize();
            fputs(buffer.getRef(), stderr);
            fflush(stderr);
            return;
        }

        // NOTE: we must disable reports otherwise we get endless recurrence, because any call
        // below may trigger ELOG_REPORT_XXX() which is redirected to the default logger
        ELOG_SCOPED_DISABLE_REPORT();

        // format log message as similar as possible to the default format
        ELogBuffer logBuffer;
        beginFormat(reportLogger, logLevel, logBuffer);

        // append user message
        logBuffer.appendV(fmt, args);

        // finish formatting and write to stderr
        endFormat(logBuffer, logLevel, file, line, function);
        fputs(logBuffer.getRef(), stderr);
    }

    void onReport(const ELogReportLogger& reportLogger, ELogLevel logLevel, const char* file,
                  int line, const char* function, const char* msg) override {
        // special case: logging before elog has initialized or after it has terminated
        if (!elog::isInitialized()) {
            // NOTE: a log buffer is used for formatting in order to emit full message in one call
            // and avoid intermixing messages from several threads
            ELogBuffer buffer;
            buffer.appendArgs("<ELOG> %s: %s\n", elogLevelToStr(logLevel), msg);
            buffer.appendArgs("Error location: file: %s, line: %d, function: %s\n", file, line,
                              function);
            buffer.finalize();
            fputs(buffer.getRef(), stderr);
            fflush(stderr);
            return;
        }

        // NOTE: we must disable reports otherwise we get endless recurrence, because any call
        // below may trigger ELOG_REPORT_XXX() which is redirected to the default logger
        ELOG_SCOPED_DISABLE_REPORT();

        // format log message as similar as possible to the default format
        ELogBuffer logBuffer;
        beginFormat(reportLogger, logLevel, logBuffer);

        // append user message
        logBuffer.append(msg);

        // finish formatting and write to stderr
        endFormat(logBuffer, logLevel, file, line, function);
        fputs(logBuffer.getRef(), stderr);
    }

private:
    void beginFormat(const ELogReportLogger& reportLogger, ELogLevel logLevel,
                     ELogBuffer& logBuffer) {
        // format log message as similar as possible to the default format
        ELogTime logTime;
        elogGetCurrentTime(logTime);
        ELogTimeBuffer timeBuffer;
        elogTimeToString(logTime, timeBuffer);
        elog_thread_id_t threadId = getCurrentThreadId();
        logBuffer.appendArgs("%s %-6s [%" ELogPRItid "] elog.%s [%s] ", timeBuffer.m_buffer,
                             elogLevelToStr(logLevel), threadId, reportLogger.getName(),
                             getThreadNameField(threadId));
    }

    void endFormat(ELogBuffer& logBuffer, ELogLevel logLevel, const char* file, int line,
                   const char* function) {
        logBuffer.append("\n");
        if (logLevel <= ELEVEL_ERROR) {
            logBuffer.appendArgs("Error location: file: %s, line: %d, function: %s\n", file, line,
                                 function);
        }
        logBuffer.finalize();
    }
};

class ELogSelfReportHandler : public ELogReportHandler {
public:
    ELogSelfReportHandler() : m_logTarget(nullptr), m_logger(nullptr) {}
    ELogSelfReportHandler(const ELogSelfReportHandler&) = delete;
    ELogSelfReportHandler(ELogSelfReportHandler&&) = delete;
    ELogSelfReportHandler& operator=(const ELogSelfReportHandler&) = delete;
    ~ELogSelfReportHandler() final {}

    bool initialize() {
        // at this point we can create a logger and restrict to stderr
        m_logger = elog::getSharedLogger("elog");
        if (m_logger == nullptr) {
            return false;
        }
        if (!createStdErrTarget()) {
            return false;
        }
        restrictToStdErr(m_logger);
        return true;
    }

    inline ELogLogger* getLogger() { return m_logger; }

    void onReportV(const ELogReportLogger& reportLogger, ELogLevel logLevel, const char* file,
                   int line, const char* function, const char* fmt, va_list args) override {
        ELogLogger* logger = getValidLogger(const_cast<ELogReportLogger&>(reportLogger));
        // TODO: this may have no effect due to the global report level
        if (logger->canLog(logLevel)) {
            logger->logFormatV(logLevel, file, line, function, fmt, args);
        }
    }

    void onReport(const ELogReportLogger& reportLogger, ELogLevel logLevel, const char* file,
                  int line, const char* function, const char* msg) override {
        ELogLogger* logger = getValidLogger(const_cast<ELogReportLogger&>(reportLogger));
        // TODO: this may have no effect due to the global report level
        if (logger->canLog(logLevel)) {
            logger->logFormat(logLevel, file, line, function, msg);
        }
    }

private:
    ELogTarget* m_logTarget;
    ELogLogger* m_logger;

    ELogLogger* getValidLogger(ELogReportLogger& reportLogger) {
        ELogLogger* logger = reportLogger.getLogger();
        if (logger == nullptr) {
            return m_logger;
        }
        if (reportLogger.requiresInit()) {
            if (reportLogger.startInit()) {
                restrictToStdErr(logger);
                reportLogger.finishInit();
            } else {
                reportLogger.waitFinishInit();
            }
        }
        return logger;
    }

    inline bool createStdErrTarget() {
        // create a dedicated stderr target with colored formatting

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
            "${level:6}${fmt:fg-color=default} "
            "[${tid}] ${src:font=underline} "
            "[${tname}] "
            "${msg}"
            "${fmt:default}&"
            "enable_stats=no&"
            "flush_policy=immediate";
        ELogTargetId logTargetId = elog::configureLogTarget(cfg);
        if (logTargetId == ELOG_INVALID_TARGET_ID) {
            ELOG_REPORT_ERROR("Failed to configure log target for ELog reports");
            return false;
        }

        // get the log target
        m_logTarget = elog::getLogTarget(logTargetId);
        if (m_logTarget == nullptr) {
            ELOG_REPORT_ERROR("Could not find ELog reports target by id %u", logTargetId);
            return false;
        }

        // make sure no one else sends to this target
        m_logTarget->setPassKey();

        // make sure no one pulls the rug under our feet (e.g. through clearAllLogTargets())
        m_logTarget->setSystemTarget();
        return true;
    }

    inline void restrictToStdErr(ELogLogger* logger) {
        // bind the logger to this specific target
        ELogTargetAffinityMask mask = 0;
        ELOG_ADD_TARGET_AFFINITY_MASK(mask, m_logTarget->getId());
        logger->getLogSource()->setLogTargetAffinity(mask);
        logger->getLogSource()->addPassKey(m_logTarget->getPassKey());
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

void ELogReport::report(const ELogReportLogger& reportLogger, ELogLevel logLevel, const char* file,
                        int line, const char* function, const char* fmt, ...) {
    if (sDisableReportCount == 0 && logLevel <= sReportLevel) {
        va_list args;
        va_start(args, fmt);

        if (sIsReporting || sDefaultReportCount > 0) {
            sDefaultReportHandler.onReportV(reportLogger, logLevel, file, line, function, fmt,
                                            args);
        } else {
            sIsReporting = true;
            ELogReportHandler* reportHandler =
                sReportHandler ? sReportHandler : &sDefaultReportHandler;
            reportHandler->onReportV(reportLogger, logLevel, file, line, function, fmt, args);
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
    if (sSelfReportHandler.initialize()) {
        setReportHandler(&sSelfReportHandler);
    }
}

void ELogReport::disableCurrentThreadReports() { ++sDisableReportCount; }

void ELogReport::enableCurrentThreadReports() {
    if (sDisableReportCount > 0) {
        --sDisableReportCount;
    }
}

void ELogReport::startUseDefaultReportHandler() { ++sDefaultReportCount; }

void ELogReport::stopUseDefaultReportHandler() {
    if (sDefaultReportCount > 0) {
        --sDefaultReportCount;
    }
}

void ELogReport::termReport() { setReportHandler(&sDefaultReportHandler); }

}  // namespace elog
