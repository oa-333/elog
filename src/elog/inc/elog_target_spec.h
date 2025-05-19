#ifndef __ELOG_TARGET_SPEC_H__
#define __ELOG_TARGET_SPEC_H__

#include <cstdint>

#include "elog_props.h"

namespace elog {

/** @struct Log Target specification (used for loading from configuration). */
struct ELOG_API ELogTargetSpec {
    /** @brief The target scheme (sys, file, db, msgq, etc.) */
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

/** @brief Target specification style constants. */
enum ELOG_API ELogTargetSpecStyle {
    /**
     * @brief URL target specification style (properties may contain asynchronous logging
     * specification).
     */
    ELOG_STYLE_URL,

    /**
     * @brief Nested target specification style (asynchronous logging specification is places in a
     * nested target specification, also compound flush policies and filter may be specified).
     */
    ELOG_STYLE_NESTED
};

/** @brief Nested target specification. */
struct ELOG_API ELogTargetNestedSpec {
    /** @brief The target specification */
    ELogTargetSpec m_spec;

    /** @typedef List of nested specification objects. */
    typedef std::vector<ELogTargetNestedSpec> SubSpecList;

    /** @typedef Map of nested specification object lists. */
    typedef std::unordered_map<std::string, SubSpecList> SubSpecMap;

    /**
     * @brief Optional nested target specification (may be an array, which implies using @ref
     * ELogCombinedTarget). Currently this is also used for compound filters and flush policies.
     */
    SubSpecMap m_subSpec;
};

}  // namespace elog

#endif  // __ELOG_TARGET_SPEC_H__