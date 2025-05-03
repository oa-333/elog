#ifndef __ELOG_COMMON_H__
#define __ELOG_COMMON_H__

#include <cstdint>
#include <string>
#include <vector>

namespace elog {

/** @brief A single property (key-value pair). */
typedef std::pair<std::string, std::string> ELogProperty;

/** @typedef Property map. */
typedef std::vector<ELogProperty> ELogPropertyMap;

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