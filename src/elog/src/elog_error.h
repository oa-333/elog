#ifndef __ELOG_ERROR_H__
#define __ELOG_ERROR_H__

#include <cstdarg>

#include "elog_def.h"
#include "elog_error_handler.h"

namespace elog {

class ELogError {
public:
    /** @brief Installs an error handler. */
    static void setErrorHandler(ELogErrorHandler* errorHandler);

    /** @brief Retrieves the installed an error handler. */
    static ELogErrorHandler* getErrorHandler();

    /** @brief Configures elog tracing. */
    static void setTraceMode(bool enableTrace = true);

    /** @brief Queries whether trace mode is enabled. */
    static bool isTraceEnabled();

    /** @brief Reports an error to enclosing application/library. */
    static void reportError(const char* errorMsgFmt, ...);

    /** @brief Reports a system call error to enclosing application/library. */
    static void reportSysError(const char* sysCall, const char* errorMsgFmt, ...);

    /** @brief Reports a system call error to enclosing application/library. */
    static void reportSysErrorCode(const char* sysCall, int errCode, const char* errorMsgFmt, ...);

    /** @brief Report a warning message to enclosing application/library. */
    static void reportWarn(const char* fmt, ...);

    /** @brief Trace a debug message. */
    static void reportTrace(const char* fmt, ...);

    /** @brief Converts system error code to string. */
    static char* sysErrorToStr(int sysErrorCode);

#ifdef ELOG_WINDOWS
    /** @brief Converts Windows system error code to string. */
    static char* win32SysErrorToStr(unsigned long sysErrorCode);

    /** @brief Deallocates Windows system error string. */
    static void win32FreeErrorStr(char* errStr);
#endif

private:
    static void initError();

    enum ReportType { RT_ERROR, RT_WARN, RT_TRACE };

    /** @brief Reports an error/warn/trace message. */
    static void reportV(ReportType reportType, const char* msgFmt, va_list ap);

    friend class ELogSystem;
};

/** @brief Report error message to enclosing application/library. */
#define ELOG_REPORT_ERROR(fmt, ...) ELogError::reportError(fmt, ##__VA_ARGS__)

/** @brief Report system call failure with error code to enclosing application/library. */
#define ELOG_REPORT_SYS_ERROR_NUM(sysCall, sysErr, fmt, ...)                \
    ELOG_REPORT_ERROR("System call " #sysCall "() failed: %d (%s)", sysErr, \
                      elog::ELogError::sysErrorToStr(sysErr));              \
    ELOG_REPORT_ERROR(fmt, ##__VA_ARGS__);

/**
 * @brief Report system call failure (error code taken from errno) to enclosing
 * application/library.
 */
#define ELOG_REPORT_SYS_ERROR(sysCall, fmt, ...) \
    ELOG_REPORT_SYS_ERROR_NUM(sysCall, errno, fmt, ##__VA_ARGS__)

#ifdef ELOG_WINDOWS
#define ELOG_REPORT_WIN32_ERROR_NUM(sysCall, sysErr, fmt, ...)                                   \
    {                                                                                            \
        char* errStr = elog::ELogError::win32SysErrorToStr(sysErr);                              \
        ELOG_REPORT_ERROR("Windows system call " #sysCall "() failed: %d (%s)", sysErr, errStr); \
        elog::ELogError::win32FreeErrorStr(errStr);                                              \
        ELOG_REPORT_ERROR(fmt, ##__VA_ARGS__);                                                   \
    }

#define ELOG_REPORT_WIN32_ERROR(sysCall, fmt, ...) \
    ELOG_REPORT_WIN32_ERROR_NUM(sysCall, ::GetLastError(), fmt, ##__VA_ARGS__);

#endif  // ELOG_WINDOWS

/** @brief Report warnning message to enclosing application/library. */
#define ELOG_REPORT_WARN(fmt, ...)                       \
    if (elog::ELogError::isTraceEnabled()) {             \
        elog::ELogError::reportWarn(fmt, ##__VA_ARGS__); \
    }

/** @brief Report error message to enclosing application/library. */
#define ELOG_REPORT_TRACE(fmt, ...)                       \
    if (elog::ELogError::isTraceEnabled()) {              \
        elog::ELogError::reportTrace(fmt, ##__VA_ARGS__); \
    }

}  // namespace elog

#endif  // __ELOG_ERROR_H__