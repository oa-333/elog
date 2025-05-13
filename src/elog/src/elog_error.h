#ifndef __ELOG_ERROR_H__
#define __ELOG_ERROR_H__

#include "elog_system.h"

namespace elog {

/** @brief Report error message to enclosing application/library. */
#define ELOG_REPORT_ERROR(fmt, ...) ELogSystem::reportError(fmt, ##__VA_ARGS__)

/** @brief Report system call failure with error code to enclosing application/library. */
#define ELOG_REPORT_SYS_ERROR_NUM(sysCall, sysErr, fmt, ...)                \
    ELOG_REPORT_ERROR("System call " #sysCall "() failed: %d (%s)", sysErr, \
                      elog::ELogSystem::sysErrorToStr(sysErr));             \
    ELOG_REPORT_ERROR(fmt, ##__VA_ARGS__);

/**
 * @brief Report system call failure (error code taken from errno) to enclosing
 * application/library.
 */
#define ELOG_REPORT_SYS_ERROR(sysCall, fmt, ...) \
    ELOG_REPORT_SYS_ERROR_NUM(sysCall, errno, fmt, ##__VA_ARGS__)

}  // namespace elog

#endif  // __ELOG_ERROR_H__