#ifndef __ELOG_FIELD_SELECTOR_INTERNAL_H___
#define __ELOG_FIELD_SELECTOR_INTERNAL_H___

#ifdef ELOG_ENABLE_STACK_TRACE
#include "dbg_util.h"
#endif

namespace elog {

/** @brief Initialize all field selectors (for internal use only). */
extern bool initFieldSelectors();

/** @brief Destroys all field selectors (for internal use only). */
extern void termFieldSelectors();

/** @brief Retrieves host name (for internal use only). */
extern const char* getHostName();

/** @brief Retrieves user name (for internal use only). */
extern const char* getUserName();

/** @brief Retrieves program name (for internal use only). */
extern const char* getProgramName();

/** @brief Installs the current thread name (for internal use only). */
extern void setCurrentThreadNameField(const char* threadName);

/** @brief Retrieves the currently installed thread name (for internal use only). */
extern const char* getCurrentThreadNameField();

#ifdef ELOG_ENABLE_STACK_TRACE
extern const char* getThreadNameField(dbgutil::os_thread_id_t threadId);
#endif

}  // namespace elog

#endif  // __ELOG_FIELD_SELECTOR_INTERNAL_H___