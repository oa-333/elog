#include "elog_rpc_formatter.h"

#include "elog_common.h"
#include "elog_error.h"

namespace elog {

bool ELogRpcFormatter::handleText(const std::string& text) {
    // text must be a comma only, perhaps surrounded with whitespace
    // ignore all white space parts
    if (trim(text).compare(",") != 0) {
        ELOG_REPORT_ERROR(
            "Invalid RPC parameter specification, expected comma between parameters: %s",
            text.c_str());
        return false;
    }
    m_lastFieldType = FieldType::FT_COMMA;
    return true;
}

bool ELogRpcFormatter::handleField(const char* fieldName, int justify) {
    // we expect alternating header name and field, so verify that
    if (m_lastFieldType == FieldType::FT_FIELD) {
        ELOG_REPORT_ERROR(
            "Invalid RPC parameter specification, expected comma between parameters: %s",
            fieldName);
        return false;
    }
    m_lastFieldType = FieldType::FT_FIELD;
    return ELogBaseFormatter::handleField(fieldName, justify);
}

}  // namespace elog
