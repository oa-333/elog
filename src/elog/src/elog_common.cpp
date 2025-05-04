#include "elog_common.h"

#include "elog_system.h"

namespace elog {

bool parseIntProp(const char* propName, const std::string& logTargetCfg, const std::string& prop,
                  uint32_t& value, bool issueError /* = true */) {
    std::size_t pos = 0;
    try {
        value = std::stoul(prop, &pos);
    } catch (std::exception& e) {
        if (issueError) {
            ELogSystem::reportError("Invalid %s value %s: %s (%s)", propName, prop.c_str(),
                                    logTargetCfg.c_str(), e.what());
        }
        return false;
    }
    if (pos != prop.length()) {
        if (issueError) {
            ELogSystem::reportError("Excess characters at %s value %s: %s", propName, prop.c_str(),
                                    logTargetCfg.c_str());
        }
        return false;
    }
    return true;
}
}  // namespace elog
