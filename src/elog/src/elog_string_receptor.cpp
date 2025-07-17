#include "elog_string_receptor.h"

#include <charconv>
#include <cstring>

#include "elog_error.h"
#include "elog_system.h"

namespace elog {

void ELogStringReceptor::receiveStringField(uint32_t typeId, const char* field,
                                            const ELogFieldSpec& fieldSpec, size_t length) {
    if (length == 0) {
        length = strlen(field);
    }
    applySpec(fieldSpec, field, length);
}

void ELogStringReceptor::receiveIntField(uint32_t typeId, uint64_t field,
                                         const ELogFieldSpec& fieldSpec) {
    // quite interestingly, using to_chars is slower... at least on MSVC/MinGW
#if __cpp_lib_to_chars >= 201611L && 0
    const int FIELD_SIZE = 64;
    char strField[FIELD_SIZE];
    std::to_chars(strField, strField + FIELD_SIZE, field);
    applySpec(fieldSpec, strField);
#else
    std::string strField = std::to_string(field);
    applySpec(fieldSpec, strField.c_str(), strField.length());
#endif
}

void ELogStringReceptor::receiveTimeField(uint32_t typeId, const ELogTime& logTime,
                                          const char* timeStr, const ELogFieldSpec& fieldSpec,
                                          size_t length) {
    applySpec(fieldSpec, timeStr, length);
}

void ELogStringReceptor::receiveLogLevelField(uint32_t typeId, ELogLevel logLevel,
                                              const ELogFieldSpec& fieldSpec) {
    applySpec(fieldSpec, elogLevelToStr(logLevel));
}

void ELogStringReceptor::applySpec(const ELogFieldSpec& fieldSpec, const char* strField,
                                   size_t fieldLen /* = 0 */) {
    // update field length if needed
    if (fieldLen == 0) {
        fieldLen = strlen(strField);
    }
    if (fieldLen > UINT32_MAX) {
        // drop field
        ELOG_REPORT_WARN("Dropping field %s with abnormal length %zu", fieldSpec.m_name.c_str(),
                         fieldLen);
        return;
    }
    uint32_t fieldLen32 = (uint32_t)fieldLen;

    // apply right justification if needed
    if (fieldSpec.m_justifySpec.m_mode == ELogJustifyMode::JM_RIGHT &&
        fieldLen32 < fieldSpec.m_justifySpec.m_justify) {
        m_logMsg.append(fieldSpec.m_justifySpec.m_justify - fieldLen32, ' ');
    }

    // apply text formatting (font/color)
    if (fieldSpec.m_textSpec != nullptr) {
        m_logMsg.append(fieldSpec.m_textSpec->m_resolvedSpec.c_str(),
                        fieldSpec.m_textSpec->m_resolvedSpec.length());
    }

    // append field to log message
    m_logMsg.append(strField, fieldLen32);

    // auto-reset text formatting if required
    if (fieldSpec.m_textSpec != nullptr && fieldSpec.m_textSpec->m_autoReset) {
        m_logMsg.append(ELogTextSpec::m_resetSpec);
    }

    // apply left justification if needed
    if (fieldSpec.m_justifySpec.m_mode == ELogJustifyMode::JM_LEFT &&
        fieldLen32 < fieldSpec.m_justifySpec.m_justify) {
        m_logMsg.append(fieldSpec.m_justifySpec.m_justify - fieldLen32, ' ');
    }
}

}  // namespace elog
