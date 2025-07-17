#include "elog_db_formatter.h"

#include <cassert>

namespace elog {

bool ELogDbFormatter::handleText(const std::string& text) {
    m_processedStatement += text;
    return true;
}

bool ELogDbFormatter::handleField(const ELogFieldSpec& fieldSpec) {
    if (m_queryStyle == QueryStyle::QS_QMARK) {
        m_processedStatement += "?";
    } else {
        m_processedStatement += "$";
        m_processedStatement += std::to_string(m_fieldNum++);
    }
    return ELogBaseFormatter::handleField(fieldSpec);
}

void ELogDbFormatter::getParamTypes(std::vector<ParamType>& paramTypes) const {
    for (ELogFieldSelector* fieldSelector : m_fieldSelectors) {
        switch (fieldSelector->getFieldType()) {
            case ELogFieldType::FT_TEXT:
                paramTypes.push_back(ParamType::PT_TEXT);
                break;

            case ELogFieldType::FT_INT:
                paramTypes.push_back(ParamType::PT_INT);
                break;

            case ELogFieldType::FT_DATETIME:
                paramTypes.push_back(ParamType::PT_DATETIME);
                break;

            case ELogFieldType::FT_LOG_LEVEL:
                paramTypes.push_back(ParamType::PT_LOG_LEVEL);
                break;

            case ELogFieldType::FT_FORMAT:
                // format fields can be ignored, as they do not represent a real field entity
                break;

            default:
                assert(false);
        }
    }
}

}  // namespace elog
