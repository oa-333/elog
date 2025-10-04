#include "db/elog_db_formatter.h"

#include <cassert>

#include "elog_report.h"

namespace elog {

ELOG_IMPLEMENT_LOG_FORMATTER(ELogDbFormatter)

ELOG_DECLARE_REPORT_LOGGER(ELogDbFormatter)

bool ELogDbFormatter::handleText(const std::string& text) {
    // all query styles collect static text into the statement string
    if (m_queryStyle != QueryStyle::QS_NONE) {
        m_processedStatement += text;
    }
    // only printf query style also generates static text fields
    if (m_queryStyle == QueryStyle::QS_PRINTF) {
        return ELogFormatter::handleText(text);
    }
    return true;
}

bool ELogDbFormatter::handleField(const ELogFieldSpec& fieldSpec) {
    if (m_queryStyle == QueryStyle::QS_QMARK) {
        m_processedStatement += "?";
    } else if (m_queryStyle == QueryStyle::QS_DOLLAR_ORDINAL) {
        m_processedStatement += "$";
        m_processedStatement += std::to_string(m_fieldNum++);
    } else if (m_queryStyle == QueryStyle::QS_PRINTF) {
        // every param is string in redis
        m_processedStatement += "%%s";
    }
    return ELogFormatter::handleField(fieldSpec);
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
