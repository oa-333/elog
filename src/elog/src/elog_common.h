#ifndef __ELOG_COMMON_H__
#define __ELOG_COMMON_H__

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>

#include "elog_common_def.h"
#include "elog_def.h"
#include "elog_props.h"

#ifdef ELOG_MINGW
// we need windows headers for MinGW
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
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
#ifdef ELOG_ENABLE_LIFE_SIGN
#define ELOG_LIFE_SIGN_REPORT_CONFIG_NAME "life_sign_report"
#define ELOG_LIFE_SIGN_LOG_FORMAT_CONFIG_NAME "life_sign_log_format"
#define ELOG_LIFE_SIGN_SYNC_PERIOD_CONFIG_NAME "life_sign_sync_period"
#endif
#ifdef ELOG_ENABLE_CONFIG_SERVICE
#define ELOG_ENABLE_CONFIG_SERVICE_NAME "enable_config_service"
#define ELOG_CONFIG_SERVICE_INTERFACE_NAME "config_service_interface"
#define ELOG_CONFIG_SERVICE_PORT_NAME "config_service_port"
#define ELOG_ENABLE_CONFIG_SERVICE_PUBLISHER_NAME "enable_config_service_publisher"
#define ELOG_CONFIG_SERVICE_PUBLISHER_NAME "config_service_publisher"
#endif

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

/** @typedef Platform-independent thread id type. */
#ifdef ELOG_WINDOWS
typedef unsigned long elog_thread_id_t;
#define ELogPRItid "lu"
#else
typedef long elog_thread_id_t;
#define ELogPRItid "ld"
#endif

inline elog_thread_id_t getCurrentThreadId() {
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

inline void getPropsByPrefix(const ELogPropertySequence& props, const char* propPrefix,
                             std::vector<std::string>& propValues) {
    ELogPropertySequence::const_iterator itr = std::find_if(
        props.begin(), props.end(),
        [propPrefix](const ELogProperty& prop) { return prop.first.starts_with(propPrefix) == 0; });
    while (itr != props.end()) {
        propValues.emplace_back(itr->second);
        ++itr;
        itr = std::find_if(itr, props.end(), [propPrefix](const ELogProperty& prop) {
            return prop.first.starts_with(propPrefix) == 0;
        });
    }
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

/** @brief Helper function for getting an integer property. */
template <typename T>
inline bool getIntProp(const ELogPropertySequence& props, const char* propName, T& propValue,
                       bool* found = nullptr) {
    std::string propValueStr;
    if (getProp(props, propName, propValueStr)) {
        if (!parseIntProp(propName, "", propValueStr, propValue)) {
            return false;
        }
        if (found) {
            *found = true;
        }
    }
    return true;
}

/** @brief Helper function for getting a boolean property. */
inline bool getBoolProp(const ELogPropertySequence& props, const char* propName, bool& propValue,
                        bool* found = nullptr) {
    std::string propValueStr;
    if (getProp(props, propName, propValueStr)) {
        if (!parseBoolProp(propName, "", propValueStr, propValue)) {
            return false;
        }
        if (found) {
            *found = true;
        }
    }
    return true;
}

/** @brief Parses time units string. */
extern bool parseTimeUnits(const char* timeUnitsStr, ELogTimeUnits& timeUnits,
                           bool issueError = true);

/** @brief Converts time units to string. */
extern const char* timeUnitToString(ELogTimeUnits timeUnits);

/** @brief Helper function for parsing a timeout property, with suffix ms, us, ns. */
extern bool parseTimeValueProp(const char* propName, const std::string& logTargetCfg,
                               const std::string& prop, uint64_t& timeValue,
                               ELogTimeUnits& origUnits, ELogTimeUnits targetUnits,
                               bool issueError = true);

/** @brief Helper function for parsing a timeout property, with suffix ms, us, ns. */
extern bool parseSizeProp(const char* propName, const std::string& logTargetCfg,
                          const std::string& prop, uint64_t& size, ELogSizeUnits targetUnits,
                          bool issueError = true);

/** @brief Converts time value from one unit to another. */
extern bool convertTimeUnit(uint64_t value, ELogTimeUnits sourceUnits, ELogTimeUnits targetUnits,
                            uint64_t& res, bool issueError = true);

/**
 * @brief Verifies a configuration value range and possibly rectifies to a default value, in which
 * case a warning will be issued.
 */
extern bool verifyUInt64PropRange(const char* targetName, const char* propName, uint64_t& value,
                                  uint64_t minValue, uint64_t maxValue,
                                  bool allowDefaultValue = false, uint64_t defaultValue = 0);

/**
 * @brief Verifies a configuration value range and possibly rectifies to a default value, in which
 * case a warning will be issued.
 */
extern bool verifyUInt32PropRange(const char* targetName, const char* propName, uint32_t& value,
                                  uint32_t minValue, uint32_t maxValue,
                                  bool allowDefaultValue = false, uint32_t defaultValue = 0);

#ifdef ELOG_ENABLE_LIFE_SIGN
/** @brief Parses life-sign scope string. */
extern bool parseLifeSignScope(const char* lifeSignScopeStr, ELogLifeSignScope& scope);

/** @brief Parses life-sign frequency spec string. */
extern bool parseFrequencySpec(const char* freqSpecStr, ELogFrequencySpec& freqSpec);
#endif

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

inline std::string toLower(const std::string& s) {
    std::string res = s;
    std::transform(res.begin(), res.end(), res.begin(),
                   [](char c) { return (char)std::tolower(c); });
    return res;
}

/**
 * @brief Break a string into tokens by whitespace.
 * @param str The input string.
 * @param tokens The output token array.
 */
void tokenize(const char* str, std::vector<std::string>& tokens, const char* delims = " \t\r\n");

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

/**
 * @brief Prepares an environment variable name from configuration name. Essentially adds "ELOG_"
 * prefix, and turns to uppercase.
 */
inline void prepareEnvVarName(const char* configName, std::string& envVarName) {
    const std::string elogEnvPrefix = "ELOG_";
    envVarName = elogEnvPrefix + configName;
    std::transform(envVarName.begin(), envVarName.end(), envVarName.begin(),
                   [](int c) { return (char)::toupper(c); });
}

/** @brief Retrieves an environment variable value by configuration value. */
inline bool getStringEnv(const char* configName, std::string& value, bool normalizeEnvVar = true,
                         bool* found = nullptr) {
    std::string envVarName;
    if (normalizeEnvVar) {
        prepareEnvVarName(configName, envVarName);
    } else {
        envVarName = configName;
    }
    return elog_getenv(envVarName.c_str(), value);
}

/** @brief Retrieves an integer environment variable value by configuration value. */
template <typename T>
inline bool getIntEnv(const char* configName, T& value, bool normalizeEnvVar = true,
                      bool* found = nullptr) {
    std::string valueStr;
    if (getStringEnv(configName, valueStr)) {
        if (!parseIntProp(configName, "", valueStr, value)) {
            return false;
        }
        if (found != nullptr) {
            *found = true;
        }
    }
    return true;
}

/** @brief Retrieves a boolean environment variable value by configuration value. */
inline bool getBoolEnv(const char* configName, bool& value, bool normalizeEnvVar = true,
                       bool* found = nullptr) {
    std::string valueStr;
    if (getStringEnv(configName, valueStr)) {
        if (!parseBoolProp(configName, "", valueStr, value)) {
            return false;
        }
        if (found != nullptr) {
            *found = true;
        }
    }
    return true;
}

}  // namespace elog

#endif  // __ELOG_COMMON_H__