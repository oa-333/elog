#ifndef __ELOG_COMMON_DEF_H__
#define __ELOG_COMMON_DEF_H__

#include <cstdint>

#include "elog_def.h"

// put some basic definitions here to avoid including files just for some type definitions

namespace elog {

/** @typedef Log source identifier type. */
typedef uint32_t ELogSourceId;

/** @define Invalid log source identifier value. */
#define ELOG_INVALID_SOURCE_ID ((ELogSourceId) - 1)

/** @typedef Log target identifier type. */
typedef uint32_t ELogTargetId;

/** @def Invalid log target identifier value. */
#define ELOG_INVALID_TARGET_ID ((elog::ELogTargetId)0xFFFFFFFF)

}  // namespace elog

#endif  // __ELOG_COMMON_DEF_H__