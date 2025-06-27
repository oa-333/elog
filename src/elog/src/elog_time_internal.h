#ifndef __ELOG_TIME_INTERNAL_H__
#define __ELOG_TIME_INTERNAL_H__

namespace elog {

/** @brief Initialize date lookup table. */
extern bool initDateTable();

/** @brief Destroys date lookup table. */
extern void termDateTable();

}  // namespace elog

#endif  // __ELOG_TIME_INTERNAL_H__