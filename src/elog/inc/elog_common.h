#ifndef __ELOG_COMMON_H__
#define __ELOG_COMMON_H__

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace elog {

/** @brief A single property (key-value pair). */
typedef std::pair<std::string, std::string> ELogProperty;

/** @typedef Property sequence (order matters). */
typedef std::vector<ELogProperty> ELogPropertySequence;

/** @typedef Property map. */
typedef std::unordered_map<std::string, std::string> ELogPropertyMap;

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
                         const std::string& prop, uint32_t& value, bool issueError = true);

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

/** @struct Log Target specification (used for loading from configuration). */
struct ELogTargetSpec {
    /** @brief The target schema (sys, file, db, msgq, etc.) */
    std::string m_scheme;

    /** @brief The server host name or address (optional). */
    std::string m_host;

    /** @brief The server port (optional). */
    uint32_t m_port;

    /** @brief The path. */
    std::string m_path;

    /** @brief Additional properties */
    ELogPropertyMap m_props;
};

}  // namespace elog

#endif  // __ELOG_COMMON_H__