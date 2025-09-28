#ifndef __ELOG_FIELD_SELECTOR_INTERNAL_H___
#define __ELOG_FIELD_SELECTOR_INTERNAL_H___

#ifdef ELOG_ENABLE_STACK_TRACE
#include "dbg_util.h"
#endif

#ifdef ELOG_ENABLE_LIFE_SIGN
#include "os_thread_manager.h"
#endif

#include <cstdint>
#include <unordered_map>

namespace elog {

/** @brief Initialize all field selectors (for internal use only). */
extern bool initFieldSelectors();

/** @brief Destroys all field selectors (for internal use only). */
extern void termFieldSelectors();

/** @brief Retrieves host name (for internal use only). */
extern const char* getHostName();

/** @brief Retrieves user name (for internal use only). */
extern const char* getUserName();

/** @brief Retrieves operating system name (for internal use only). */
extern const char* getOsName();

/** @brief Retrieves operating system version (for internal use only). */
extern const char* getOsVersion();

/** @brief Retrieves application name (for internal use only). */
extern const char* getAppName();

/** @brief Retrieves program name (for internal use only). */
extern const char* getProgramName();

/** @brief Retrieves the process id field (for internal use only). */
extern uint32_t getProcessIdField();

/** @brief Installs the application's name (for internal use only). */
extern void setAppNameField(const char* appName);

/** @brief Installs the current thread name (for internal use only). */
extern bool setCurrentThreadNameField(const char* threadName);

/** @brief Retrieves the currently installed thread name (for internal use only). */
extern const char* getThreadNameField(uint32_t threadId);

#ifdef ELOG_ENABLE_LIFE_SIGN
/**
 * @brief Installs a notifier for the current thread so incoming signals can be processed (mostly
 * required on Windows).
 */
extern bool setCurrentThreadNotifierImpl(dbgutil::ThreadNotifier* notifier);

/**
 * @brief Installs a notifier for the current thread so incoming signals can be processed (mostly
 * required on Windows).
 */
extern bool setThreadNotifierImpl(const char* threadName, dbgutil::ThreadNotifier* notifier);

/** @brief Retrieves thread identifier by name. Returns true if found (thread-safe). */
extern bool getThreadDataByName(const char* threadName, uint32_t& threadId,
                                dbgutil::ThreadNotifier*& notifier);

/** @typedef Thread data map for life-sign reports. */
typedef std::unordered_map<uint32_t, std::pair<std::string, dbgutil::ThreadNotifier*>>
    ThreadDataMap;

/**
 * @brief Retrieves a list of thread identifiers whose name matches a regular expression. Each
 * returned entry contains the actual name of the thread. This call is thread-safe.
 */
extern void getThreadDataByNameRegEx(const char* threadNameRegEx, ThreadDataMap& threadData);
#endif

}  // namespace elog

#endif  // __ELOG_FIELD_SELECTOR_INTERNAL_H___