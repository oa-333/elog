#ifndef __ELOG_TARGET_SPEC_H__
#define __ELOG_TARGET_SPEC_H__

#include <cstdint>

#include "elog_def.h"
#include "elog_props.h"

namespace elog {

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
    ELogTargetUrlSpec(const ELogTargetUrlSpec&) = delete;
    ELogTargetUrlSpec(ELogTargetUrlSpec&&) = delete;
    ELogTargetUrlSpec& operator=(const ELogTargetUrlSpec&) = delete;

    ~ELogTargetUrlSpec() {
        if (m_subUrlSpec != nullptr) {
            delete m_subUrlSpec;
            m_subUrlSpec = nullptr;
        }
    }
};

}  // namespace elog

#endif  // __ELOG_TARGET_SPEC_H__