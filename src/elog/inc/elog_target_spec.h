#ifndef __ELOG_TARGET_SPEC_H__
#define __ELOG_TARGET_SPEC_H__

#include <cstdint>

#include "elog_def.h"
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
enum class ELogTargetSpecStyle : uint32_t {
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

/** @struct Log Target URL specification (used for loading from configuration). */
struct ELOG_API ELogTargetUrlSpec {
    /** @brief The target scheme (sys, file, db, msgq, etc.) */
    ELogStringPropertyPos m_scheme;

    /** @brief The path. May be further divided to user, password, host and port. */
    ELogStringPropertyPos m_path;

    /** @brief The user name (optional). */
    ELogStringPropertyPos m_user;

    /** @brief The password (optional). */
    ELogStringPropertyPos m_passwd;

    /** @brief The server host name or address (optional). */
    ELogStringPropertyPos m_host;

    /** @brief The server port (optional). */
    ELogIntPropertyPos m_port;

    /** @brief Additional properties (no order preserved). */
    ELogPropertyPosMap m_props;

    /** @brief A nested URL specification. */
    ELogTargetUrlSpec* m_subUrlSpec;

    ELogTargetUrlSpec() : m_subUrlSpec(nullptr) {}

    ~ELogTargetUrlSpec() {
        if (m_subUrlSpec != nullptr) {
            delete m_subUrlSpec;
            m_subUrlSpec = nullptr;
        }
    }
};

}  // namespace elog

#endif  // __ELOG_TARGET_SPEC_H__