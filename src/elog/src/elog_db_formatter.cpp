#include "elog_db_formatter.h"

namespace elog {

void ELogDbFormatter::handleText(const std::string& text) { m_processedStatement += text; }

bool ELogDbFormatter::handleField(const char* fieldName, int justify) {
    if (m_queryStyle == QueryStyle::QS_QMARK) {
        m_processedStatement += "?";
    } else {
        m_processedStatement += "$";
        m_processedStatement += std::to_string(m_fieldNum++);
    }
    return ELogBaseFormatter::handleField(fieldName, justify);
}

}  // namespace elog
