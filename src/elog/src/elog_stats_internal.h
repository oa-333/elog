#ifndef __ELOG_STATS_INTERNAL_H__
#define __ELOG_STATS_INTERNAL_H__

#include <cstdint>

namespace elog {

/** @brief Initializes statistics collection mechanism. */
extern bool initializeStats(uint32_t maxThreads);

/** @brief Terminates the TLS key for cleanup during thread termination. */
extern void terminateStats();

}  // namespace elog

#endif  // __ELOG_STATS_INTERNAL_H__