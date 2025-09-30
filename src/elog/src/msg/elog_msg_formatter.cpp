#include "msg/elog_msg_formatter.h"

#include "elog_common.h"
#include "elog_report.h"

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogMsgFormatter)

ELOG_IMPLEMENT_LOG_FORMATTER(ELogMsgFormatter)

bool ELogMsgFormatter::handleText(const std::string& text) {
    // text must be a comma only, perhaps surrounded with whitespace
    // ignore all white space parts
    if (trim(text).compare(",") != 0) {
        ELOG_REPORT_ERROR("Invalid parameter specification, expected comma between parameters: %s",
                          text.c_str());
        return false;
    }
    m_lastFieldType = FieldType::FT_COMMA;
    return true;
}

bool ELogMsgFormatter::handleField(const ELogFieldSpec& fieldSpec) {
    if (m_lastFieldType == FieldType::FT_FIELD) {
        ELOG_REPORT_ERROR("Invalid parameter specification, expected comma between parameters: %s",
                          fieldSpec.m_name.c_str());
        return false;
    }
    m_lastFieldType = FieldType::FT_FIELD;
    return ELogFormatter::handleField(fieldSpec);
}

}  // namespace elog
