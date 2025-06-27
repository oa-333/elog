#include "elog_string_stream_receptor.h"

#include <iomanip>

#include "elog_system.h"

namespace elog {

void ELogStringStreamReceptor::receiveStringField(uint32_t typeId, const char* field,
                                                  const ELogFieldSpec& fieldSpec, size_t length) {
    applyJustify(fieldSpec);
    m_msgStream << field;
}

void ELogStringStreamReceptor::receiveIntField(uint32_t typeId, uint64_t field,
                                               const ELogFieldSpec& fieldSpec) {
    applyJustify(fieldSpec);
    m_msgStream << std::to_string(field);
}

void ELogStringStreamReceptor::receiveTimeField(uint32_t typeId, const ELogTime& logTime,
                                                const char* timeStr, const ELogFieldSpec& fieldSpec,
                                                size_t length) {
    applyJustify(fieldSpec);
    m_msgStream << timeStr;
}

void ELogStringStreamReceptor::receiveLogLevelField(uint32_t typeId, ELogLevel logLevel,
                                                    const ELogFieldSpec& fieldSpec) {
    applyJustify(fieldSpec);
    m_msgStream << elogLevelToStr(logLevel);
}

void ELogStringStreamReceptor::applyJustify(const ELogFieldSpec& fieldSpec) {
    m_msgStream << std::setw(fieldSpec.m_justify);
    if (fieldSpec.m_justifyMode == ELogJustifyMode::JM_LEFT) {
        // left justify
        m_msgStream << std::left;
    } else if (fieldSpec.m_justifyMode == ELogJustifyMode::JM_RIGHT) {
        // right justify
        m_msgStream << std::right;
    }
}

}  // namespace elog
