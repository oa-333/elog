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

/** @brief Helper function for retrieving a  */
inline bool elogGetProp(const ELogPropertySequence& props, const char* propName,
                        std::string& propValue) {
    ELogPropertySequence::const_iterator itr = std::find_if(
        props.begin(), props.end(),
        [propName](const ELogProperty& prop) { return prop.second.compare(propName) == 0; });
    if (itr != props.end()) {
        propValue = itr->second;
        return true;
    }
    return false;
}

inline void insertPropOverride(ELogPropertyMap& props, const std::string& key,
                               const std::string& value) {
    std::pair<ELogPropertyMap::iterator, bool> itrRes =
        props.insert(ELogPropertyMap::value_type(key, value));
    if (!itrRes.second) {
        itrRes.first->second = value;
    }
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