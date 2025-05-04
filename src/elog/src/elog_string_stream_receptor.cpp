#include "elog_string_stream_receptor.h"

#include <iomanip>

#include "elog_system.h"

namespace elog {

void ELogStringStreamReceptor::receiveStringField(const std::string& field, int justify) {
    applyJustify(justify);
    m_msgStream << field;
}

void ELogStringStreamReceptor::receiveIntField(uint64_t field, int justify) {
    applyJustify(justify);
    m_msgStream << std::to_string(field);
}

#ifdef ELOG_MSVC
void ELogStringStreamReceptor::receiveTimeField(const SYSTEMTIME& sysTime, const char* timeStr,
                                                int justify) {
    applyJustify(justify);
    m_msgStream << timeStr;
}
#else
void ELogStringStreamReceptor::receiveTimeField(const timeval& sysTime, const char* timeStr,
                                                int justify) {
    applyJustify(justify);
    m_msgStream << timeStr;
}
#endif

void ELogStringStreamReceptor::receiveLogLevelField(ELogLevel logLevel, int justify) {
    applyJustify(justify);
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
