#ifndef __ELOG_LOG_FORMATTER_INTERNAL_H__
#define __ELOG_LOG_FORMATTER_INTERNAL_H__

namespace elog {

/** @brief Initialize all log formatters (for internal use only). */
extern bool initLogFormatters();

/** @brief Destroys all log formatters (for internal use only). */
extern void termLogFormatters();

}  // namespace elog

#endif  // __ELOG_LOG_FORMATTER_INTERNAL_H__