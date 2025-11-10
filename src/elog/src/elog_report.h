#ifndef __ELOG_ERROR_H__
#define __ELOG_ERROR_H__

#include <cerrno>
#include <cstdarg>
#include <string>

#include "elog_def.h"
#include "elog_internal.h"
#include "elog_level.h"
#include "elog_rate_limiter.h"  // for moderate macros
#include "elog_report_handler.h"

namespace elog {

class ELogReport {
public:
    /** @brief Installs an report handler. */
    static void setReportHandler(ELogReportHandler* reportHandler);

    /** @brief Retrieves the installed a report handler. */
    static ELogReportHandler* getReportHandler();

    /** @brief Configures elog internal log message report level. */
    static void setReportLevel(ELogLevel reportLevel);

    /** @brief Retrieves elog internal log message report level. */
    static ELogLevel getReportLevel();

    /** @brief Reports an ELog's internal log message. */
    static void report(const ELogReportLogger& logger, ELogLevel logLevel, const char* file,
                       int line, const char* function, const char* fmt, ...);

    /** @brief Converts system error code to string. */
    static char* sysErrorToStr(int sysErrorCode);

#ifdef ELOG_WINDOWS
    /** @brief Converts Windows system error code to string. */
    static char* win32SysErrorToStr(unsigned long sysErrorCode);

    /** @brief Deallocates Windows system error string. */
    static void win32FreeErrorStr(char* errStr);
#endif

    /** @brief Disables reports for current thread. */
    static void disableCurrentThreadReports();

    /** @brief Enabled reports for current thread. */
    static void enableCurrentThreadReports();

    /** @brief Forces usage of the default report handler. */
    static void startUseDefaultReportHandler();

    /** @brief Stops forcing usage of the default report handler. */
    static void stopUseDefaultReportHandler();

private:
    // initialize/terminate reporting mechanism
    static void initReport();
    static void termReport();

    // allow initialization function special access
    friend bool initGlobals();
    friend void termGlobals();
};

/** @brief Helper macro for declaring internal logger by name. */
#define ELOG_DECLARE_REPORT_LOGGER(name) static ELogReportLogger sLogger(#name);

/** @brief Helper macro for getting a reference to the internal logger. */
#define ELOG_REPORT_LOGGER sLogger

/** @brief Generic reporting macro. */
#define ELOG_REPORT_EX(logger, level, fmt, ...) \
    ELogReport::report(logger, level, __FILE__, __LINE__, ELOG_FUNCTION, fmt, ##__VA_ARGS__)

/** @brief Generic reporting macro. */
#define ELOG_REPORT(level, fmt, ...) ELOG_REPORT_EX(sLogger, level, fmt, ##__VA_ARGS__)

/** @brief Report error message to enclosing application/library. */
#define ELOG_REPORT_FATAL(fmt, ...) ELOG_REPORT(ELEVEL_FATAL, fmt, ##__VA_ARGS__)
#define ELOG_REPORT_ERROR(fmt, ...) ELOG_REPORT(ELEVEL_ERROR, fmt, ##__VA_ARGS__)
#define ELOG_REPORT_WARN(fmt, ...) ELOG_REPORT(ELEVEL_WARN, fmt, ##__VA_ARGS__)
#define ELOG_REPORT_NOTICE(fmt, ...) ELOG_REPORT(ELEVEL_NOTICE, fmt, ##__VA_ARGS__)
#define ELOG_REPORT_INFO(fmt, ...) ELOG_REPORT(ELEVEL_INFO, fmt, ##__VA_ARGS__)
#define ELOG_REPORT_TRACE(fmt, ...) ELOG_REPORT(ELEVEL_TRACE, fmt, ##__VA_ARGS__)
#define ELOG_REPORT_DEBUG(fmt, ...) ELOG_REPORT(ELEVEL_DEBUG, fmt, ##__VA_ARGS__)
#define ELOG_REPORT_DIAG(fmt, ...) ELOG_REPORT(ELEVEL_DIAG, fmt, ##__VA_ARGS__)

/** @brief Report system call failure with error code to enclosing application/library. */
#define ELOG_REPORT_SYS_ERROR_NUM(sysCall, sysErr, fmt, ...)                \
    ELOG_REPORT_ERROR("System call " #sysCall "() failed: %d (%s)", sysErr, \
                      ELogReport::sysErrorToStr(sysErr));                   \
    ELOG_REPORT_ERROR(fmt, ##__VA_ARGS__);

/**
 * @brief Report system call failure (error code taken from errno) to enclosing
 * application/library.
 */
#define ELOG_REPORT_SYS_ERROR(sysCall, fmt, ...)                        \
    {                                                                   \
        int sysErr = errno;                                             \
        ELOG_REPORT_SYS_ERROR_NUM(sysCall, sysErr, fmt, ##__VA_ARGS__); \
    }

#ifdef ELOG_WINDOWS
/**
 * @brief Report Windows system call failure (error code provided by user) to enclosing
 * application/library.
 */
#define ELOG_REPORT_WIN32_ERROR_NUM(sysCall, sysErr, fmt, ...)                                    \
    {                                                                                             \
        char* errStr = ELogReport::win32SysErrorToStr(sysErr);                                    \
        ELOG_REPORT_ERROR("Windows system call " #sysCall "() failed: %lu (%s)", sysErr, errStr); \
        ELogReport::win32FreeErrorStr(errStr);                                                    \
        ELOG_REPORT_ERROR(fmt, ##__VA_ARGS__);                                                    \
    }

/**
 * @brief Report Windows system call failure (error code taken from GetLastError()) to enclosing
 * application/library.
 */
#define ELOG_REPORT_WIN32_ERROR(sysCall, fmt, ...)                        \
    {                                                                     \
        DWORD sysErr = ::GetLastError();                                  \
        ELOG_REPORT_WIN32_ERROR_NUM(sysCall, sysErr, fmt, ##__VA_ARGS__); \
    }

#endif  // ELOG_WINDOWS

/**
 * @brief Reports internal log message, only once in the entire life-time of the application.
 * @param logger The logger used for message formatting.
 * @param level The log level. If the log level is insufficient, then the message is dropped.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_REPORT_ONCE_EX(logger, level, fmt, ...)           \
    {                                                          \
        static ELogOnce once;                                  \
        if (once) {                                            \
            ELOG_REPORT_EX(logger, level, fmt, ##__VA_ARGS__); \
        }                                                      \
    }

/** @brief Generic reporting macro (reporting only once). */
#define ELOG_REPORT_ONCE(level, fmt, ...) ELOG_REPORT_ONCE_EX(sLogger, level, fmt, ##__VA_ARGS__)

/** @brief Report error message to enclosing application/library (report only once). */
#define ELOG_REPORT_ONCE_FATAL(fmt, ...) ELOG_REPORT_ONCE(ELEVEL_FATAL, fmt, ##__VA_ARGS__)
#define ELOG_REPORT_ONCE_ERROR(fmt, ...) ELOG_REPORT_ONCE(ELEVEL_ERROR, fmt, ##__VA_ARGS__)
#define ELOG_REPORT_ONCE_WARN(fmt, ...) ELOG_REPORT_ONCE(ELEVEL_WARN, fmt, ##__VA_ARGS__)
#define ELOG_REPORT_ONCE_NOTICE(fmt, ...) ELOG_REPORT_ONCE(ELEVEL_NOTICE, fmt, ##__VA_ARGS__)
#define ELOG_REPORT_ONCE_INFO(fmt, ...) ELOG_REPORT_ONCE(ELEVEL_INFO, fmt, ##__VA_ARGS__)
#define ELOG_REPORT_ONCE_TRACE(fmt, ...) ELOG_REPORT_ONCE(ELEVEL_TRACE, fmt, ##__VA_ARGS__)
#define ELOG_REPORT_ONCE_DEBUG(fmt, ...) ELOG_REPORT_ONCE(ELEVEL_DEBUG, fmt, ##__VA_ARGS__)
#define ELOG_REPORT_ONCE_DIAG(fmt, ...) ELOG_REPORT_ONCE(ELEVEL_DIAG, fmt, ##__VA_ARGS__)

/**
 * @brief Report system call failure with error code to enclosing application/library (report only
 * once).
 */
#define ELOG_REPORT_ONCE_SYS_ERROR_NUM(sysCall, sysErr, fmt, ...)           \
    {                                                                       \
        static ELogOnce once;                                               \
        if (once) {                                                         \
            ELOG_REPORT_SYS_ERROR_NUM(sysCall, sysErr, fmt, ##__VA_ARGS__); \
        }                                                                   \
    }

/**
 * @brief Report system call failure (error code taken from errno) to enclosing application/library
 * (report only once).
 */
#define ELOG_REPORT_ONCE_SYS_ERROR(sysCall, fmt, ...)                        \
    {                                                                        \
        int sysErr = errno;                                                  \
        ELOG_REPORT_ONCE_SYS_ERROR_NUM(sysCall, sysErr, fmt, ##__VA_ARGS__); \
    }

#ifdef ELOG_WINDOWS
/**
 * @brief Report Windows system call failure (error code provided by user) to enclosing
 * application/library.
 */
#define ELOG_REPORT_ONCE_WIN32_ERROR_NUM(sysCall, sysErr, fmt, ...)           \
    {                                                                         \
        static ELogOnce once;                                                 \
        if (once) {                                                           \
            ELOG_REPORT_WIN32_ERROR_NUM(sysCall, sysErr, fmt, ##__VA_ARGS__); \
        }                                                                     \
    }

/**
 * @brief Report Windows system call failure (error code taken from GetLastError()) to enclosing
 * application/library.
 */
#define ELOG_REPORT_ONCE_WIN32_ERROR(sysCall, fmt, ...)                        \
    {                                                                          \
        DWORD sysErr = ::GetLastError();                                       \
        ELOG_REPORT_ONCE_WIN32_ERROR_NUM(sysCall, sysErr, fmt, ##__VA_ARGS__); \
    }

#endif  // ELOG_WINDOWS

/**
 * @brief Reports internal log message, only once in the entire life-time of the current thread.
 * @param logger The logger used for message formatting.
 * @param level The log level. If the log level is insufficient, then the message is dropped.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_REPORT_ONCE_THREAD_EX(logger, level, fmt, ...)    \
    {                                                          \
        static thread_local bool once = false;                 \
        if (!once) {                                           \
            once = true;                                       \
            ELOG_REPORT_EX(logger, level, fmt, ##__VA_ARGS__); \
        }                                                      \
    }

/** @brief Generic reporting macro (reporting only once per-thread). */
#define ELOG_REPORT_ONCE_THREAD(level, fmt, ...) \
    ELOG_REPORT_ONCE_THREAD_EX(sLogger, level, fmt, ##__VA_ARGS__)

/** @brief Report error message to enclosing application/library (report only once per-thread). */
#define ELOG_REPORT_ONCE_THREAD_FATAL(fmt, ...) \
    ELOG_REPORT_ONCE_THREAD(ELEVEL_FATAL, fmt, ##__VA_ARGS__)
#define ELOG_REPORT_ONCE_THREAD_ERROR(fmt, ...) \
    ELOG_REPORT_ONCE_THREAD(ELEVEL_ERROR, fmt, ##__VA_ARGS__)
#define ELOG_REPORT_ONCE_THREAD_WARN(fmt, ...) \
    ELOG_REPORT_ONCE_THREAD(ELEVEL_WARN, fmt, ##__VA_ARGS__)
#define ELOG_REPORT_ONCE_THREAD_NOTICE(fmt, ...) \
    ELOG_REPORT_ONCE_THREAD(ELEVEL_NOTICE, fmt, ##__VA_ARGS__)
#define ELOG_REPORT_ONCE_THREAD_INFO(fmt, ...) \
    ELOG_REPORT_ONCE_THREAD(ELEVEL_INFO, fmt, ##__VA_ARGS__)
#define ELOG_REPORT_ONCE_THREAD_TRACE(fmt, ...) \
    ELOG_REPORT_ONCE_THREAD(ELEVEL_TRACE, fmt, ##__VA_ARGS__)
#define ELOG_REPORT_ONCE_THREAD_DEBUG(fmt, ...) \
    ELOG_REPORT_ONCE_THREAD(ELEVEL_DEBUG, fmt, ##__VA_ARGS__)
#define ELOG_REPORT_ONCE_THREAD_DIAG(fmt, ...) \
    ELOG_REPORT_ONCE_THREAD(ELEVEL_DIAG, fmt, ##__VA_ARGS__)

/**
 * @brief Report system call failure with error code to enclosing application/library (report only
 * once per thread).
 */
#define ELOG_REPORT_ONCE_THREAD_SYS_ERROR_NUM(sysCall, sysErr, fmt, ...)    \
    {                                                                       \
        static thread_local bool once = false;                              \
        if (!once) {                                                        \
            once = true;                                                    \
            ELOG_REPORT_SYS_ERROR_NUM(sysCall, sysErr, fmt, ##__VA_ARGS__); \
        }                                                                   \
    }

/**
 * @brief Report system call failure (error code taken from errno) to enclosing application/library
 * (report only once).
 */
#define ELOG_REPORT_ONCE_THREAD_SYS_ERROR(sysCall, fmt, ...)                        \
    {                                                                               \
        int sysErr = errno;                                                         \
        ELOG_REPORT_ONCE_THREAD_SYS_ERROR_NUM(sysCall, sysErr, fmt, ##__VA_ARGS__); \
    }

#ifdef ELOG_WINDOWS
/**
 * @brief Report Windows system call failure (error code provided by user) to enclosing
 * application/library.
 */
#define ELOG_REPORT_ONCE_THREAD_WIN32_ERROR_NUM(sysCall, sysErr, fmt, ...)    \
    {                                                                         \
        static thread_local bool once = false;                                \
        if (!once) {                                                          \
            once = true;                                                      \
            ELOG_REPORT_WIN32_ERROR_NUM(sysCall, sysErr, fmt, ##__VA_ARGS__); \
        }                                                                     \
    }

/**
 * @brief Report Windows system call failure (error code taken from GetLastError()) to enclosing
 * application/library.
 */
#define ELOG_REPORT_ONCE_THREAD_WIN32_ERROR(sysCall, fmt, ...)                        \
    {                                                                                 \
        DWORD sysErr = ::GetLastError();                                              \
        ELOG_REPORT_ONCE_THREAD_WIN32_ERROR_NUM(sysCall, sysErr, fmt, ##__VA_ARGS__); \
    }

#endif  // ELOG_WINDOWS

/**
 * @brief Reports internal log message, while moderating its occurrence.
 * @param logger The logger used for message formatting.
 * @param level The log level. If the log level is insufficient, then the message is dropped.
 * @param maxMsg The maximum number of messages that can be printed in a time interval.
 * @param timeout The rate limit timeout interval.
 * @param units The rate limit timeout units.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_REPORT_MODERATE_EX(logger, level, maxMsg, timeout, units, fmt, ...) \
    {                                                                            \
        static ELogModerate mod(fmt, maxMsg, timeout, units);                    \
        if (mod.moderate()) {                                                    \
            ELOG_REPORT_EX(logger, level, fmt, ##__VA_ARGS__);                   \
        }                                                                        \
    }

/** @brief Generic reporting macro (moderated). */
#define ELOG_REPORT_MODERATE(level, maxMsg, timeout, units, fmt, ...) \
    ELOG_REPORT_MODERATE_EX(sLogger, level, maxMsg, timeout, units, fmt, ##__VA_ARGS__)

/** @brief Report error message to enclosing application/library (moderated). */
#define ELOG_REPORT_MODERATE_FATAL(maxMsg, timeout, units, fmt, ...) \
    ELOG_REPORT_MODERATE(ELEVEL_FATAL, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#define ELOG_REPORT_MODERATE_ERROR(maxMsg, timeout, units, fmt, ...) \
    ELOG_REPORT_MODERATE(ELEVEL_ERROR, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#define ELOG_REPORT_MODERATE_WARN(maxMsg, timeout, units, fmt, ...) \
    ELOG_REPORT_MODERATE(ELEVEL_WARN, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#define ELOG_REPORT_MODERATE_NOTICE(maxMsg, timeout, units, fmt, ...) \
    ELOG_REPORT_MODERATE(ELEVEL_NOTICE, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#define ELOG_REPORT_MODERATE_INFO(maxMsg, timeout, units, fmt, ...) \
    ELOG_REPORT_MODERATE(ELEVEL_INFO, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#define ELOG_REPORT_MODERATE_TRACE(maxMsg, timeout, units, fmt, ...) \
    ELOG_REPORT_MODERATE(ELEVEL_TRACE, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#define ELOG_REPORT_MODERATE_DEBUG(maxMsg, timeout, units, fmt, ...) \
    ELOG_REPORT_MODERATE(ELEVEL_DEBUG, maxMsg, timeout, units, fmt, ##__VA_ARGS__)
#define ELOG_REPORT_MODERATE_DIAG(maxMsg, timeout, units, fmt, ...) \
    ELOG_REPORT_MODERATE(ELEVEL_DIAG, maxMsg, timeout, units, fmt, ##__VA_ARGS__)

// use error moderation as configured by user
#define ELOG_REPORT_MODERATE_ERROR_DEFAULT(fmt, ...)                                     \
    {                                                                                    \
        const ELogRateLimitParams& errorRateParams = getParams().m_errorModerationRate;  \
        ELOG_REPORT_MODERATE_ERROR(errorRateParams.m_maxMsgs, errorRateParams.m_timeout, \
                                   errorRateParams.m_units, fmt, ##__VA_ARGS__);         \
    }

/**
 * @brief Report system call failure with error code to enclosing application/library (report only
 * once per thread).
 */
#define ELOG_REPORT_MODERATE_SYS_ERROR_NUM(sysCall, sysErr, maxMsg, timeout, units, fmt, ...) \
    {                                                                                         \
        static ELogModerate mod(fmt, maxMsg, timeout, units);                                 \
        if (mod.moderate()) {                                                                 \
            ELOG_REPORT_SYS_ERROR_NUM(sysCall, sysErr, fmt, ##__VA_ARGS__);                   \
        }                                                                                     \
    }

/**
 * @brief Report system call failure (error code taken from errno) to enclosing application/library
 * (report only once).
 */
#define ELOG_REPORT_MODERATE_SYS_ERROR(sysCall, maxMsg, timeout, units, fmt, ...)        \
    {                                                                                    \
        int sysErr = errno;                                                              \
        ELOG_REPORT_MODERATE_SYS_ERROR_NUM(sysCall, sysErr, maxMsg, timeout, units, fmt, \
                                           ##__VA_ARGS__);                               \
    }

// use error moderation as configured by user
#define ELOG_REPORT_MODERATE_SYS_ERROR_DEFAULT(sysCall, fmt, ...)                               \
    {                                                                                           \
        const ELogRateLimitParams& errorRateParams = getParams().m_errorModerationRate;         \
        ELOG_REPORT_MODERATE_SYS_ERROR(sysCall, errorRateParams.m_maxMsgs,                      \
                                       errorRateParams.m_timeout, errorRateParams.m_units, fmt, \
                                       ##__VA_ARGS__);                                          \
    }

#ifdef ELOG_WINDOWS
/**
 * @brief Report Windows system call failure (error code provided by user) to enclosing
 * application/library.
 */
#define ELOG_REPORT_MODERATE_WIN32_ERROR_NUM(sysCall, sysErr, maxMsg, timeout, units, fmt, ...) \
    {                                                                                           \
        static ELogModerate mod(fmt, maxMsg, timeout, units);                                   \
        if (mod.moderate()) {                                                                   \
            ELOG_REPORT_WIN32_ERROR_NUM(sysCall, sysErr, fmt, ##__VA_ARGS__);                   \
        }                                                                                       \
    }

/**
 * @brief Report Windows system call failure (error code taken from GetLastError()) to enclosing
 * application/library.
 */
#define ELOG_REPORT_MODERATE_WIN32_ERROR(sysCall, maxMsg, timeout, units, fmt, ...)        \
    {                                                                                      \
        DWORD sysErr = ::GetLastError();                                                   \
        ELOG_REPORT_MODERATE_WIN32_ERROR_NUM(sysCall, sysErr, maxMsg, timeout, units, fmt, \
                                             ##__VA_ARGS__);                               \
    }

// use error moderation as configured by user
#define ELOG_REPORT_MODERATE_WIN32_ERROR_DEFAULT(sysCall, fmt, ...)                               \
    {                                                                                             \
        const ELogRateLimitParams& errorRateParams = getParams().m_errorModerationRate;           \
        ELOG_REPORT_MODERATE_WIN32_ERROR(sysCall, errorRateParams.m_maxMsgs,                      \
                                         errorRateParams.m_timeout, errorRateParams.m_units, fmt, \
                                         ##__VA_ARGS__);                                          \
    }

#endif  // ELOG_WINDOWS

/**
 * @brief Reports internal log message, once in every N calls.
 * @param logger The logger used for message formatting.
 * @param level The log level. If the log level is insufficient, then the message is dropped.
 * @param N Specifies to log once in N log messages.
 * @param fmt The log message format string.
 * @param ... Log message format string parameters.
 */
#define ELOG_REPORT_EVERY_N_EX(logger, level, N, fmt, ...)            \
    {                                                                 \
        static std::atomic<uint64_t> count = 0;                       \
        if (count.fetch_add(1, std::memory_order_relaxed) % N == 0) { \
            ELOG_REPORT_EX(validLogger, level, fmt, ##__VA_ARGS__);   \
        }                                                             \
    }

/** @brief Generic reporting macro (once in every N calls). */
#define ELOG_REPORT_EVERY_N(level, N, fmt, ...) \
    ELOG_REPORT_EVERY_N_EX(sLogger, level, N, fmt, ##__VA_ARGS__)

/** @brief Report error message to enclosing application/library (once in every N calls). */
#define ELOG_REPORT_EVERY_N_FATAL(N, fmt, ...) \
    ELOG_REPORT_EVERY_N(ELEVEL_FATAL, N, fmt, ##__VA_ARGS__)
#define ELOG_REPORT_EVERY_N_ERROR(N, fmt, ...) \
    ELOG_REPORT_EVERY_N(ELEVEL_ERROR, N, fmt, ##__VA_ARGS__)
#define ELOG_REPORT_EVERY_N_WARN(N, fmt, ...) \
    ELOG_REPORT_EVERY_N(ELEVEL_WARN, N, fmt, ##__VA_ARGS__)
#define ELOG_REPORT_EVERY_N_NOTICE(N, fmt, ...) \
    ELOG_REPORT_EVERY_N(ELEVEL_NOTICE, N, fmt, ##__VA_ARGS__)
#define ELOG_REPORT_EVERY_N_INFO(N, fmt, ...) \
    ELOG_REPORT_EVERY_N(ELEVEL_INFO, N, fmt, ##__VA_ARGS__)
#define ELOG_REPORT_EVERY_N_TRACE(N, fmt, ...) \
    ELOG_REPORT_EVERY_N(ELEVEL_TRACE, N, fmt, ##__VA_ARGS__)
#define ELOG_REPORT_EVERY_N_DEBUG(N, fmt, ...) \
    ELOG_REPORT_EVERY_N(ELEVEL_DEBUG, N, fmt, ##__VA_ARGS__)
#define ELOG_REPORT_EVERY_N_DIAG(N, fmt, ...) \
    ELOG_REPORT_EVERY_N(ELEVEL_DIAG, N, fmt, ##__VA_ARGS__)

/**
 * @brief Report system call failure with error code to enclosing application/library (report only
 * once per thread).
 */
#define ELOG_REPORT_EVERY_N_SYS_ERROR_NUM(sysCall, sysErr, N, fmt, ...)     \
    {                                                                       \
        static std::atomic<uint64_t> count = 0;                             \
        if (count.fetch_add(1, std::memory_order_relaxed) % N == 0) {       \
            ELOG_REPORT_SYS_ERROR_NUM(sysCall, sysErr, fmt, ##__VA_ARGS__); \
        }                                                                   \
    }

/**
 * @brief Report system call failure (error code taken from errno) to enclosing application/library
 * (report only once).
 */
#define ELOG_REPORT_EVERY_N_SYS_ERROR(sysCall, N, fmt, ...)                        \
    {                                                                              \
        int sysErr = errno;                                                        \
        ELOG_REPORT_EVERY_N_SYS_ERROR_NUM(sysCall, sysErr, N, fmt, ##__VA_ARGS__); \
    }

#ifdef ELOG_WINDOWS
/**
 * @brief Report Windows system call failure (error code provided by user) to enclosing
 * application/library.
 */
#define ELOG_REPORT_EVERY_N_WIN32_ERROR_NUM(sysCall, sysErr, N, fmt, ...)     \
    {                                                                         \
        static std::atomic<uint64_t> count = 0;                               \
        if (count.fetch_add(1, std::memory_order_relaxed) % N == 0) {         \
            ELOG_REPORT_WIN32_ERROR_NUM(sysCall, sysErr, fmt, ##__VA_ARGS__); \
        }                                                                     \
    }

/**
 * @brief Report Windows system call failure (error code taken from GetLastError()) to enclosing
 * application/library.
 */
#define ELOG_REPORT_EVERY_N_WIN32_ERROR(sysCall, N, fmt, ...)                        \
    {                                                                                \
        DWORD sysErr = ::GetLastError();                                             \
        ELOG_REPORT_EVERY_N_WIN32_ERROR_NUM(sysCall, sysErr, N, fmt, ##__VA_ARGS__); \
    }

#endif  // ELOG_WINDOWS

// helper RAII class
struct ScopedDisableReport {
    ScopedDisableReport() { ELogReport::disableCurrentThreadReports(); }
    ScopedDisableReport(const ScopedDisableReport&) = delete;
    ScopedDisableReport(ScopedDisableReport&&) = delete;
    ScopedDisableReport& operator=(const ScopedDisableReport&) = delete;
    ~ScopedDisableReport() { ELogReport::enableCurrentThreadReports(); }
};

/** @def Disable ELog internal reporting altogether for the current block. */
#define ELOG_SCOPED_DISABLE_REPORT() ScopedDisableReport _disableReport

// helper RAII class
struct ScopedDefaultReport {
    ScopedDefaultReport() { ELogReport::startUseDefaultReportHandler(); }
    ScopedDefaultReport(const ScopedDefaultReport&) = delete;
    ScopedDefaultReport(ScopedDefaultReport&&) = delete;
    ScopedDefaultReport& operator=(const ScopedDefaultReport&) = delete;
    ~ScopedDefaultReport() { ELogReport::stopUseDefaultReportHandler(); }
};

/** @def Force usage of default ELog internal reporting for the current block. */
#define ELOG_SCOPED_DEFAULT_REPORT() ScopedDefaultReport _defaultReport

}  // namespace elog

#endif  // __ELOG_ERROR_H__