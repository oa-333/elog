#ifndef __ELOG_COMMON_H__
#define __ELOG_COMMON_H__

#include <algorithm>
#include <cstdint>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>

#include "elog_props.h"

#define RED "\x1B[31m"
#define GRN "\x1B[32m"
#define YEL "\x1B[33m"
#define BLU "\x1B[34m"
#define MAG "\x1B[35m"
#define CYN "\x1B[36m"
#define WHT "\x1B[37m"
#define RESET "\x1B[0m"

namespace elog {

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

}  // namespace elog

#endif  // __ELOG_COMMON_H__