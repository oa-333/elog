#include "elog_string_stream_receptor.h"

#include <iomanip>

#include "elog.h"

namespace elog {

void ELogStringStreamReceptor::receiveStringField(uint32_t typeId, const char* field,
                                                  const ELogFieldSpec& fieldSpec, size_t length) {
    applySpec(fieldSpec);
    m_msgStream << field;
    applyPostSpec(fieldSpec);
}

void ELogStringStreamReceptor::receiveIntField(uint32_t typeId, uint64_t field,
                                               const ELogFieldSpec& fieldSpec) {
    applySpec(fieldSpec);
    m_msgStream << std::to_string(field);
    applyPostSpec(fieldSpec);
}

void ELogStringStreamReceptor::receiveTimeField(uint32_t typeId, const ELogTime& logTime,
                                                const char* timeStr, const ELogFieldSpec& fieldSpec,
                                                size_t length) {
    applySpec(fieldSpec);
    m_msgStream << timeStr;
    applyPostSpec(fieldSpec);
}

void ELogStringStreamReceptor::receiveLogLevelField(uint32_t typeId, ELogLevel logLevel,
                                                    const ELogFieldSpec& fieldSpec) {
    applySpec(fieldSpec);
    m_msgStream << elogLevelToStr(logLevel);
    applyPostSpec(fieldSpec);
}

void ELogStringStreamReceptor::applySpec(const ELogFieldSpec& fieldSpec) {
    m_msgStream << std::setw(fieldSpec.m_justifySpec.m_justify);
    if (fieldSpec.m_justifySpec.m_mode == ELogJustifyMode::JM_LEFT) {
        // left justify
        m_msgStream << std::left;
    } else if (fieldSpec.m_justifySpec.m_mode == ELogJustifyMode::JM_RIGHT) {
        // right justify
        m_msgStream << std::right;
    }

    // apply text formatting (font/color)
    if (fieldSpec.m_textSpec != nullptr) {
        m_msgStream << fieldSpec.m_textSpec->m_resolvedSpec;
    }
}

void ELogStringStreamReceptor::applyPostSpec(const ELogFieldSpec& fieldSpec) {
    // auto reset text formatting if required
    if (fieldSpec.m_textSpec != nullptr && fieldSpec.m_textSpec->m_autoReset) {
        m_msgStream << ELogTextSpec::m_resetSpec;
    }
}

}  // namespace elog
