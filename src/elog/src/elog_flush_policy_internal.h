#ifndef __ELOG_FLUSH_POLICY_INTERNAL_H__
#define __ELOG_FLUSH_POLICY_INTERNAL_H__

namespace elog {

/** @brief Initialize all flush policies (for internal use only). */
extern bool initFlushPolicies();

/** @brief Destroys all flush policies (for internal use only). */
extern void termFlushPolicies();

}  // namespace elog

#endif  // __ELOG_FLUSH_POLICY_INTERNAL_H__