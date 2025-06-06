#ifndef __ELOG_COMMON_DEF_H__
#define __ELOG_COMMON_DEF_H__

#include <cstdint>

#include "elog_def.h"

// put some basic definitions here to avoid including files just for some type definitions

namespace elog {

/** @typedef Log source identifier type. */
typedef uint32_t ELogSourceId;

/** @def Invalid log source identifier value. */
#define ELOG_INVALID_SOURCE_ID ((ELogSourceId) - 1)

/** @typedef Log target identifier type. */
typedef uint32_t ELogTargetId;

/** @def Invalid log target identifier value. */
#define ELOG_INVALID_TARGET_ID ((elog::ELogTargetId)0xFFFFFFFF)

/** @typedef Log target affinity mask. */
typedef uint64_t ELogTargetAffinityMask;

/** @def Affinity mask that includes all log targets. */
#define ELOG_ALL_TARGET_AFFINITY_MASK ((uint64_t)-1)

/** @def The maximum log target id that can be managed by a single mask value. */
#define ELOG_MAX_LOG_TARGET_ID_AFFINITY ((ELogTargetId)(sizeof(ELogTargetAffinityMask) - 1))

/** @def Clears a log target affinity mask from all raised bits. */
#define ELOG_CLEAR_TARGET_AFFINITY_MASK(mask) mask = 0

/** @def Converts a zero-based log target id to an affinity mask value */
#define ELOG_TARGET_ID_TO_AFFINITY_MASK(logTargetId) (1ull << (logTargetId))

/** @def Raises the bit in a log target affinity mask for the given log target id. */
#define ELOG_ADD_TARGET_AFFINITY_MASK(mask, logTargetId) \
    mask |= ELOG_TARGET_ID_TO_AFFINITY_MASK(logTargetId)

/** @def Clears the bit in a log target affinity mask for the given log target id. */
#define ELOG_REMOVE_TARGET_AFFINITY_MASK(mask, logTargetId) \
    mask &= ~ELOG_TARGET_ID_TO_AFFINITY_MASK(logTargetId)

/** @def Checks whether an affinity mask contains a log target id.  */
#define ELOG_HAS_TARGET_AFFINITY_MASK(mask, logTargetId) \
    ((mask) & ELOG_TARGET_ID_TO_AFFINITY_MASK(logTargetId))

}  // namespace elog

#endif  // __ELOG_COMMON_DEF_H__