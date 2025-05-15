#ifndef __ELOG_FILTER_INTERNAL_H__
#define __ELOG_FILTER_INTERNAL_H__

namespace elog {

/** @brief Initialize all filters (for internal use only). */
extern bool initFilters();

/** @brief Destroys all filters (for internal use only). */
extern void termFilters();

}  // namespace elog

#endif  // __ELOG_FILTER_INTERNAL_H__