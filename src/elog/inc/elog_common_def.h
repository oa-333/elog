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

/** @typedef Pass key (for source filtering, used by tracers). */
typedef uint32_t ELogPassKey;

/** @def No passkey value. */
#define ELOG_NO_PASSKEY ((elog::ELogPassKey)0)

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

/** @enum Log level propagation mode constants. */
enum class ELogPropagateMode : uint32_t {
    /** @brief Designates that log level should not be propagated to child log sources. */
    PM_NONE,

    /** @brief Designates that log level should be propagated to child log sources as is. */
    PM_SET,

    /**
     * @brief Designates that log level should be propagated to child log sources such that
     * child log sources are to be restricted not to have looser log level than that of their
     * parent.
     * @note Strict log level have lower log level value.
     */
    PM_RESTRICT,

    /**
     * @brief Designates that log level should be propagated to child log sources such that the
     * log level of child log sources should be loosened, if necessary, to ensure that it is at
     * least as loose as the log level of the parent.
     * @note Strict log level have lower log level value.
     */
    PM_LOOSE
};

/** @def Cache entry id type. */
typedef uint32_t ELogCacheEntryId;

/** @def Invalid cache entry id value. */
#define ELOG_INVALID_CACHE_ENTRY_ID ((ELogCacheEntryId)0xFFFFFFFF)

/** @enum Timeout units (used in flush policy protected helper parsing methods). */
enum class ELogTimeoutUnits : uint32_t {
    /** @brief Seconds */
    TU_SECONDS,

    /** @brief Milli-seconds */
    TU_MILLI_SECONDS,

    /** @brief Micro-seconds */
    TU_MICRO_SECONDS,

    /** @brief Nano-seconds */
    TU_NANO_SECONDS
};

/** @enum Size units (used in flush policy protected helper parsing methods). */
enum class ELogSizeUnits : uint32_t {
    /** @brief Bytes. */
    SU_BYTES,

    /** @brief Kilo-Bytes. */
    SU_KILO_BYTES,

    /** @brief Mega-Bytes. */
    SU_MEGA_BYTES,

    /** @brief Giga-Bytes. */
    SU_GIGA_BYTES
};

}  // namespace elog

#endif  // __ELOG_COMMON_DEF_H__