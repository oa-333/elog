#ifndef __ELOG_ERROR_H__
#define __ELOG_ERROR_H__

#include <cerrno>
#include <cstdarg>

#include "elog_def.h"
#include "elog_error_handler.h"
#include "elog_level.h"

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
    static void report(ELogLevel logLevel, const char* file, int line, const char* function,
                       const char* fmt, ...);

    /** @brief Converts system error code to string. */
    static char* sysErrorToStr(int sysErrorCode);

#ifdef ELOG_WINDOWS
    /** @brief Converts Windows system error code to string. */
    static char* win32SysErrorToStr(unsigned long sysErrorCode);

    /** @brief Deallocates Windows system error string. */
    static void win32FreeErrorStr(char* errStr);
#endif

private:
    // initialize/terminate reporting mechanism
    static void initReport();
    static void termReport();

    // allow initialization function special access
    friend bool initGlobals();
    friend void termGlobals();
};

#define ELOG_REPORT(level, fmt, ...) \
    ELogReport::report(level, __FILE__, __LINE__, ELOG_FUNCTION, fmt, ##__VA_ARGS__)

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
                      elog::ELogReport::sysErrorToStr(sysErr));             \
    ELOG_REPORT_ERROR(fmt, ##__VA_ARGS__);

/**
 * @brief Report system call failure (error code taken from errno) to enclosing
 * application/library.
 */
#define ELOG_REPORT_SYS_ERROR(sysCall, fmt, ...) \
    ELOG_REPORT_SYS_ERROR_NUM(sysCall, errno, fmt, ##__VA_ARGS__)

#ifdef ELOG_WINDOWS
#define ELOG_REPORT_WIN32_ERROR_NUM(sysCall, sysErr, fmt, ...)                                    \
    {                                                                                             \
        char* errStr = elog::ELogReport::win32SysErrorToStr(sysErr);                              \
        ELOG_REPORT_ERROR("Windows system call " #sysCall "() failed: %lu (%s)", sysErr, errStr); \
        elog::ELogReport::win32FreeErrorStr(errStr);                                              \
        ELOG_REPORT_ERROR(fmt, ##__VA_ARGS__);                                                    \
    }

#define ELOG_REPORT_WIN32_ERROR(sysCall, fmt, ...) \
    ELOG_REPORT_WIN32_ERROR_NUM(sysCall, ::GetLastError(), fmt, ##__VA_ARGS__);

#endif  // ELOG_WINDOWS

}  // namespace elog

#endif  // __ELOG_ERROR_H__