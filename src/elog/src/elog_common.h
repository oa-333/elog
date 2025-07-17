#ifndef __ELOG_COMMON_H__
#define __ELOG_COMMON_H__

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>

#include "elog_def.h"
#include "elog_props.h"

#ifdef ELOG_MINGW
// we need windows headers for MinGW
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif !defined(ELOG_WINDOWS)
#include <sys/syscall.h>
#include <unistd.h>
#ifdef SYS_gettid
#define gettid() syscall(SYS_gettid)
#else
#error "SYS_gettid unavailable on this platform"
#endif
#endif

#define ELOG_LEVEL_CONFIG_NAME "log_level"
#define ELOG_FORMAT_CONFIG_NAME "log_format"
#define ELOG_FILTER_CONFIG_NAME "log_filter"
#define ELOG_FLUSH_POLICY_CONFIG_NAME "log_flush_policy"
#define ELOG_TARGET_CONFIG_NAME "log_target"
#define ELOG_RATE_LIMIT_CONFIG_NAME "log_rate_limit"
#define ELOG_AFFINITY_CONFIG_NAME "log_affinity"
// #define ELOG_LEVEL_FORMAT_CONFIG_NAME "log_level_format"

// simple colors for internal use
#define RED "\x1B[31m"
#define GRN "\x1B[32m"
#define YEL "\x1B[33m"
#define BLU "\x1B[34m"
#define MAG "\x1B[35m"
#define CYN "\x1B[36m"
#define WHT "\x1B[37m"
#define RESET "\x1B[0m"

#define ELOG_DEFAULT_LOG_MSG_RESERVE_SIZE 256

namespace elog {

inline uint32_t getCurrentThreadId() {
#ifdef ELOG_WINDOWS
    return GetCurrentThreadId();
#else
    return gettid();
#endif  // ELOG_WINDOWS
}

/** @brief Helper function for retrieving a property from a sequence */
inline bool getProp(const ELogPropertySequence& props, const char* propName,
                    std::string& propValue) {
    ELogPropertySequence::const_iterator itr = std::find_if(
        props.begin(), props.end(),
        [propName](const ELogProperty& prop) { return prop.first.compare(propName) == 0; });
    if (itr != props.end()) {
        propValue = itr->second;
        return true;
    }
    return false;
}

/** @brief Helper function for parsing an integer property */
extern bool parseIntProp(const char* propName, const std::string& logTargetCfg,
                         const std::string& prop, int32_t& value, bool issueError = true);

/** @brief Helper function for parsing an integer property */
extern bool parseIntProp(const char* propName, const std::string& logTargetCfg,
                         const std::string& prop, uint32_t& value, bool issueError = true);

/** @brief Helper function for parsing an integer property */
extern bool parseIntProp(const char* propName, const std::string& logTargetCfg,
                         const std::string& prop, int64_t& value, bool issueError = true);

/** @brief Helper function for parsing an integer property */
extern bool parseIntProp(const char* propName, const std::string& logTargetCfg,
                         const std::string& prop, uint64_t& value, bool issueError = true);

/** @brief Helper function for parsing a boolean property */
extern bool parseBoolProp(const char* propName, const std::string& logTargetCfg,
                          const std::string& prop, bool& value, bool issueError = true);

/** @brief Trims a string's prefix from the left side (in-place). */
inline void ltrim(std::string& s) { s.erase(0, s.find_first_not_of(" \n\r\t")); }

/** @brief Trims a string suffix from the right side (in-place). */
inline void rtrim(std::string& s) { s.erase(s.find_last_not_of(" \n\r\t") + 1); }

/** @brief Trims a string from both sides (in-place). */
inline std::string trim(const std::string& s) {
    std::string res = s;
    ltrim(res);
    rtrim(res);
    return res;
}

/**
 * @brief Safer and possibly/hopefully faster version of strncpy() (not benchmarked yet). Unlike
 * strncpy(), this implementation has three notable differences:
 * (1) The resulting destination always has a terminating null
 * (2) In case of a short source string, the resulting destination is not padded with many nulls up
 * to the size limit, but rather only one terminating null is added
 * (3) The result value is the number of characters copied, not including the terminating null.
 * @param dest The destination string.
 * @param src The source string.
 * @param destLen The destination length.
 * @param srcLen The source length (optional, can run faster if provided).
 * @return The number of characters copied.
 */
extern size_t elog_strncpy(char* dest, const char* src, size_t destLen, size_t srcLen = 0);

/**
 * @brief Retrieves environment variable value.
 *
 * @param envVarName The environment variable name.
 * @param envVarValue The resulting environment variable value.
 * @return true If variable was found, otherwise false.
 */
extern bool elog_getenv(const char* envVarName, std::string& envVarValue);

/**
 * @brief Opens a file (optionally in a secure manner, only on Windows when ELOG_SECURE is
 * enabled).
 */
extern FILE* elog_fopen(const char* path, const char* mode);

}  // namespace elog

#endif  // __ELOG_COMMON_H__