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

            // check if this is a field reference
            if (value.find("${") == 0) {
                // verify field reference syntax
                if (value.back() != '}') {
                    ELOG_REPORT_ERROR(
                        "Invalid field specification, missing closing curly brace, while parsing "
                        "JSON string '%s'");
                    return false;
                }

                // extract field spec string and parse
                std::string valueStr = value.substr(2, value.size() - 2);
                ELogFieldSpec fieldSpec;
                if (!parseFieldSpec(valueStr, fieldSpec)) {
                    ELOG_REPORT_ERROR("Failed to parse json value '%s' for key '%s'",
                                      item.value().dump().c_str(), item.key().c_str());
                    return false;
                }

                // now collect the field
                ELOG_REPORT_TRACE("Extracted field spec: %s", fieldSpec.m_name.c_str());
                if (!handleField(fieldSpec)) {
                    return false;
                }
            } else {
                // otherwise, this is plain static text
                ELOG_REPORT_TRACE("Extracted static text value: %s", value.c_str());
                if (!handleText(value)) {
                    return false;
                }
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