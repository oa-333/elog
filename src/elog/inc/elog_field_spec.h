#ifndef __ELOG_FIELD_SPEC_H__
#define __ELOG_FIELD_SPEC_H__

#include <cinttypes>
#include <string>

#include "elog_def.h"

namespace elog {

/** @brief Log record field reference specification. */
struct ELOG_API ELogFieldSpec {
    /** @var The special field name (reference token). */
    std::string m_name;

    /** @var Justification value (used for formatting). */
    int32_t m_justify;
};

}  // namespace elog

#endif  // __ELOG_FIELD_SPEC_H__