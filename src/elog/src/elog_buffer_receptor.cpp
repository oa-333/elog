#include "elog_buffer_receptor.h"

#include <charconv>
#include <cstring>

#include "elog_report.h"

namespace elog {

void ELogBufferReceptor::receiveStringField(uint32_t typeId, const char* field,
                                            const ELogFieldSpec& fieldSpec, size_t length) {
    if (length == 0) {
        length = strlen(field);
    }
    applySpec(fieldSpec, field, length);
}

void ELogBufferReceptor::receiveIntField(uint32_t typeId, uint64_t field,
                                         const ELogFieldSpec& fieldSpec) {
#if __cpp_lib_to_chars >= 201611L
    const int FIELD_SIZE = 64;
    alignas(64) char strField[FIELD_SIZE];
    std::to_chars_result res = std::to_chars(strField, strField + FIELD_SIZE, field);
    if (res.ec == (std::errc)0) {
        *res.ptr = 0;
        applySpec(fieldSpec, strField);
    }
#else
    std::string strField = std::to_string(field);
    applySpec(fieldSpec, strField.c_str(), strField.length());
#endif
}

void ELogBufferReceptor::receiveTimeField(uint32_t typeId, const ELogTime& logTime,
                                          const char* timeStr, const ELogFieldSpec& fieldSpec,
                                          size_t length) {
    applySpec(fieldSpec, timeStr, length);
}

void ELogBufferReceptor::receiveLogLevelField(uint32_t typeId, ELogLevel logLevel,
                                              const ELogFieldSpec& fieldSpec) {
    applySpec(fieldSpec, elogLevelToStr(logLevel));
}

void ELogBufferReceptor::applySpec(const ELogFieldSpec& fieldSpec, const char* strField,
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
        m_buffer.append(fieldSpec.m_justifySpec.m_justify - fieldLen32, ' ');
    }

    // apply text formatting (font/color)
    if (fieldSpec.m_textSpec != nullptr) {
        m_buffer.append(fieldSpec.m_textSpec->m_resolvedSpec.c_str(),
                        fieldSpec.m_textSpec->m_resolvedSpec.length());
    }

    // append field to log message
    m_buffer.append(strField, fieldLen32);

    // auto-reset text formatting if required
    if (fieldSpec.m_textSpec != nullptr && fieldSpec.m_textSpec->m_autoReset) {
        m_buffer.append(ELogTextSpec::m_resetSpec);
    }

    // apply left justification if needed
    if (fieldSpec.m_justifySpec.m_mode == ELogJustifyMode::JM_LEFT &&
        fieldLen32 < fieldSpec.m_justifySpec.m_justify) {
        m_buffer.append(fieldSpec.m_justifySpec.m_justify - fieldLen32, ' ');
    }
}

}  // namespace elog
