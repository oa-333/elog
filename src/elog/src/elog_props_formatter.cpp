#include "elog_props_formatter.h"

#include "elog_common.h"
#include "elog_report.h"

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogPropsFormatter)

ELOG_IMPLEMENT_LOG_FORMATTER(ELogPropsFormatter)

bool ELogPropsFormatter::parseProps(const std::string& props) {
    // props is expected to wrapped with curly braces
    std::string trimmedProps = trim(props);
    if (trimmedProps.empty()) {
        // empty properties are allowed (not enforcing existence of braces)
        return true;
    }
    if (trimmedProps[0] != '{' || trimmedProps[trimmedProps.length() - 1] != '}') {
        ELOG_REPORT_ERROR(
            "Invalid properties specification, should be enclosed with curly braces {}: %s",
            props.c_str());
        return false;
    }
    trimmedProps = trimmedProps.substr(1, trimmedProps.length() - 2);

    // connecting to base formatter logic is awkward, we simply parse a comma separated list
    std::string::size_type prevPos = 0;  // always 1 past previous comma
    std::string::size_type commaPos = 0;
    do {
        commaPos = trimmedProps.find(',', prevPos);
        std::string propPair = trim(trimmedProps.substr(prevPos, commaPos - prevPos));

        // search for '=' or ':' separator between property name and value
        std::string::size_type sepPos = propPair.find('=');
        if (sepPos == std::string::npos) {
            // try also ':', as in JSON like format, but more permissive
            sepPos = propPair.find(':');
            if (sepPos == std::string::npos) {
                ELOG_REPORT_ERROR(
                    "Failed to parse property list, property '%s' missing expected equal or colon "
                    "sign between property name and value: %s",
                    propPair.c_str(), props.c_str());
                return false;
            }
        }

        // extract property name and value
        std::string propName = trim(propPair.substr(0, sepPos));
        std::string propValue = trim(propPair.substr(sepPos + 1));
        m_propNames.push_back(propName);

        // parse value, this already triggers handle field/text
        if (!parseValue(propValue)) {
            ELOG_REPORT_ERROR("Failed to parse property value '%s' for key '%s'", propValue.c_str(),
                              propName.c_str());
            return false;
        }

        if (commaPos != std::string::npos) {
            prevPos = commaPos + 1;
        }
    } while (commaPos != std::string::npos);
    return true;
}

}  // namespace elog
