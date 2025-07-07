#include "elog_json_formatter.h"

#ifdef ELOG_ENABLE_JSON

#include "elog_common.h"
#include "elog_error.h"

namespace elog {

bool ELogJsonFormatter::parseJson(const std::string& jsonStr) {
    try {
        // parse json and iterate over all items
        ELOG_REPORT_TRACE("Parsing json string: %s", jsonStr.c_str());
        m_jsonField = nlohmann::json::parse(jsonStr);
        for (const auto& item : m_jsonField.items()) {
            // take property name
            ELOG_REPORT_TRACE("Iterating property name %s", item.key().c_str());
            m_propNames.push_back(item.key());
            std::string value = trim(((std::string)item.value().get<std::string>()));

            // check if this is a field reference, this already triggers handle field/text
            if (!parseValue(value)) {
                ELOG_REPORT_ERROR("Failed to parse json value '%s' for key '%s'",
                                  item.value().dump().c_str(), item.key().c_str());
                return false;
            }
            ELOG_REPORT_TRACE("Parsed json property: %s=%s", item.key().c_str(), value.c_str());
        }
    } catch (nlohmann::json::parse_error& pe) {
        ELOG_REPORT_ERROR("Failed to parse json string '%s': %s", jsonStr.c_str(), pe.what());
        return false;
    }
    return true;
}

}  // namespace elog

#endif  // ELOG_ENABLE_JSON