#include "elog_string_receptor.h"

#include <charconv>
#include <cstring>

#include "elog_system.h"

namespace elog {

void ELogStringReceptor::receiveStringField(uint32_t typeId, const char* field,
                                            const ELogFieldSpec& fieldSpec, size_t length) {
    if (length == 0) {
        length = strlen(field);
    }
    applyJustify(fieldSpec, field, length);
}

void ELogStringReceptor::receiveIntField(uint32_t typeId, uint64_t field,
                                         const ELogFieldSpec& fieldSpec) {
    // quite interestingly, using to_chars is slower... at least on MSVC/MinGW
#if __cpp_lib_to_chars >= 201611L && 0
    const int FIELD_SIZE = 64;
    char strField[FIELD_SIZE];
    std::to_chars(strField, strField + FIELD_SIZE, field);
    applyJustify(fieldSpec, strField);
#else
    std::string strField = std::to_string(field);
    applyJustify(fieldSpec, strField.c_str(), strField.length());
#endif
}

void ELogStringReceptor::receiveTimeField(uint32_t typeId, const ELogTime& logTime,
                                          const char* timeStr, const ELogFieldSpec& fieldSpec) {
    applyJustify(fieldSpec, timeStr);
}

void ELogStringReceptor::receiveLogLevelField(uint32_t typeId, ELogLevel logLevel,
                                              const ELogFieldSpec& fieldSpec) {
    applyJustify(fieldSpec, elogLevelToStr(logLevel));
}

void ELogStringReceptor::applyJustify(const ELogFieldSpec& fieldSpec, const char* strField,
                                      uint32_t fieldLen /* = 0 */) {
    // update field length if needed
    if (fieldSpec.m_justifyMode != ELogJustifyMode::JM_NONE && fieldLen == 0) {
        fieldLen = strlen(strField);
    }

    // apply right justification if needed
    if (fieldSpec.m_justifyMode == ELogJustifyMode::JM_RIGHT && fieldLen < fieldSpec.m_justify) {
        m_logMsg.append(fieldSpec.m_justify - fieldLen, ' ');
    }

    // append field to log message
    m_logMsg += strField;

    // apply left justification if needed
    if (fieldSpec.m_justifyMode == ELogJustifyMode::JM_LEFT && fieldLen < fieldSpec.m_justify) {
        m_logMsg.append(fieldSpec.m_justify - fieldLen, ' ');
    }
}

}  // namespace elog
