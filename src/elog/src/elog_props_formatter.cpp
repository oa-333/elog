#include "elog_props_formatter.h"

#include "elog_common.h"
#include "elog_error.h"

namespace elog {

bool ELogPropsFormatter::handleText(const std::string& text) {
    // ignore all white space parts
    if (trim(text).empty()) {
        return true;
    }

    // verify text and field references are alternating
    if (m_lastFieldType == FieldType::FT_TEXT) {
        ELOG_REPORT_ERROR(
            "Invalid properties specification, missing field reference after property name: %s",
            text.c_str());
        return false;
    }

    // the text here is <prop-name> = (optionally prepended with a comma)
    std::string::size_type startPos = 0;
    std::string::size_type commaPos = text.find(',');
    if (commaPos != std::string::npos) {
        startPos = commaPos + 1;
    }
    std::string::size_type equalPos = text.find('=', startPos);
    if (equalPos == std::string::npos) {
        ELOG_REPORT_ERROR("Header name text '%s' missing expected equal sign", text.c_str());
        return false;
    }
    std::string propName = trim(text.substr(0, equalPos));
    m_propNames.push_back(propName);
    m_lastFieldType = FieldType::FT_TEXT;
    return true;
}

bool ELogPropsFormatter::handleField(const ELogFieldSpec& fieldSpec) {
    // we expect alternating prop name and field, so verify that
    if (m_lastFieldType != FieldType::FT_TEXT) {
        ELOG_REPORT_ERROR(
            "Invalid properties specification, missing property name before field reference: %s",
            fieldSpec.m_name.c_str());
        return false;
    }
    m_lastFieldType = FieldType::FT_FIELD;
    return ELogBaseFormatter::handleField(fieldSpec);
}

}  // namespace elog
