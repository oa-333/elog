#ifndef __ELOG_PROPS_H__
#define __ELOG_PROPS_H__

#include <string>
#include <unordered_map>
#include <vector>

namespace elog {

/** @brief A single property (key-value pair). */
typedef std::pair<std::string, std::string> ELogProperty;

/** @typedef Property sequence (order matters). */
typedef std::vector<ELogProperty> ELogPropertySequence;

/** @typedef Property map. */
typedef std::unordered_map<std::string, std::string> ELogPropertyMap;

}  // namespace elog

#endif  // __ELOG_PROPS_H__