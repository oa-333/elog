#include "elog_common.h"

#include "elog_error.h"

namespace elog {

bool parseIntProp(const char* propName, const std::string& logTargetCfg, const std::string& prop,
                  uint32_t& value, bool issueError /* = true */) {
    std::size_t pos = 0;
    try {
        value = std::stoul(prop, &pos);
    } catch (std::exception& e) {
        if (issueError) {
            ELOG_REPORT_ERROR("Invalid %s value %s: %s (%s)", propName, prop.c_str(),
                              logTargetCfg.c_str(), e.what());
        }
        return false;
    }
    if (pos != prop.length()) {
        if (issueError) {
            ELOG_REPORT_ERROR("Excess characters at %s value %s: %s", propName, prop.c_str(),
                              logTargetCfg.c_str());
        }
        return false;
    }
    return true;
}

bool parseBoolProp(const char* propName, const std::string& logTargetCfg, const std::string& prop,
                   bool& value, bool issueError /* = true */) {
    if (prop.compare("true") == 0 || prop.compare("yes") == 0) {
        value = true;
    } else if (prop.compare("false") == 0 || prop.compare("no") == 0) {
        value = false;
    } else {
        if (issueError) {
            ELOG_REPORT_ERROR("Invalid boolean property %s value %s: %s", propName, prop.c_str(),
                              logTargetCfg.c_str());
        }
        return false;
    }
    return true;
}

}  // namespace elog
