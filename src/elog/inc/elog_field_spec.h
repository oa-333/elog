#ifndef __ELOG_FIELD_SPEC_H__
#define __ELOG_FIELD_SPEC_H__

#include <cinttypes>
#include <string>

#include "elog_def.h"

namespace elog {

/** @brief Justify mode constants. */
enum class ELogJustifyMode : uint32_t {
    /** @var No justification used. */
    JM_NONE,

    /** @var Justify to the left, padding on the right. */
    JM_LEFT,

    /** @var Justify to the right, padding on the left. */
    JM_RIGHT
};

/** @brief Log record field reference specification. */
struct ELOG_API ELogFieldSpec {
    /** @var The special field name (reference token). */
    std::string m_name;

    /** @var Justification mode. */
    ELogJustifyMode m_justifyMode;

    /** @var Justification value (used for formatting). */
    uint32_t m_justify;

    ELogFieldSpec(const char* name = "", ELogJustifyMode justifyMode = ELogJustifyMode::JM_NONE,
                  uint32_t justify = 0)
        : m_name(name), m_justifyMode(justifyMode), m_justify(justify) {}
};

}  // namespace elog

#endif  // __ELOG_FIELD_SPEC_H__