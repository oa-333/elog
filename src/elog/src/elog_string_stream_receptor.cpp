#include "elog_string_stream_receptor.h"

#include <iomanip>

#include "elog_system.h"

namespace elog {

void ELogStringStreamReceptor::receiveStringField(uint32_t typeId, const std::string& field,
                                                  const ELogFieldSpec& fieldSpec) {
    applyJustify(fieldSpec.m_justify);
    m_msgStream << field;
}

void ELogStringStreamReceptor::receiveIntField(uint32_t typeId, uint64_t field,
                                               const ELogFieldSpec& fieldSpec) {
    applyJustify(fieldSpec.m_justify);
    m_msgStream << std::to_string(field);
}

void ELogStringStreamReceptor::receiveTimeField(uint32_t typeId, const ELogTime& logTime,
                                                const char* timeStr,
                                                const ELogFieldSpec& fieldSpec) {
    applyJustify(fieldSpec.m_justify);
    m_msgStream << timeStr;
}

void ELogStringStreamReceptor::receiveLogLevelField(uint32_t typeId, ELogLevel logLevel,
                                                    const ELogFieldSpec& fieldSpec) {
    applyJustify(fieldSpec.m_justify);
    m_msgStream << elogLevelToStr(logLevel);
}

void ELogStringStreamReceptor::applyJustify(int justify) {
    if (justify > 0) {
        // left justify
        m_msgStream << std::setw(justify) << std::left;
    } else if (justify < 0) {
        // right justify
        m_msgStream << std::setw(-justify) << std::right;
    } else {
        // no justify
        m_msgStream << std::setw(0);
    }
}

}  // namespace elog
