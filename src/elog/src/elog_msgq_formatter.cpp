#include "elog_msgq_formatter.h"

#include "elog_common.h"
#include "elog_error.h"

namespace elog {

bool ELogMsgQFormatter::handleText(const std::string& text) {
    // ignore all white space parts
    if (trim(text).empty()) {
        return true;
    }

    // verify text and field references are alternating
    if (m_lastFieldType == FieldType::FT_TEXT) {
        ELOG_REPORT_ERROR(
            "Invalid headers specification, missing field reference after header name: %s",
            text.c_str());
        return false;
    }

    // the text here is <header-name> = (optionally prepended with a comma)
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
    std::string headerName = trim(text.substr(0, equalPos));
    m_headerNames.push_back(headerName);
    m_lastFieldType = FieldType::FT_TEXT;
    return true;
}

bool ELogMsgQFormatter::handleField(const char* fieldName, int justify) {
    // we expect alternating header name and field, so verify that
    if (m_lastFieldType != FieldType::FT_TEXT) {
        ELOG_REPORT_ERROR(
            "Invalid headers specification, missing header name before field reference: %s",
            fieldName);
        return false;
    }
    m_lastFieldType = FieldType::FT_FIELD;
    return ELogBaseFormatter::handleField(fieldName, justify);
}

}  // namespace elog
