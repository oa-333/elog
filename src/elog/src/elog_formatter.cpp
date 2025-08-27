#include "elog_formatter.h"

#include <bit>

#include "elog_buffer_receptor.h"
#include "elog_string_receptor.h"

namespace elog {

void ELogFormatter::formatLogMsg(const ELogRecord& logRecord, std::string& logMsg) {
    // unlike the string stream receptor, the string receptor formats directly the resulting log
    // message string, and so we save one or two string copies
    ELogStringReceptor receptor(logMsg);
    applyFieldSelectors(logRecord, &receptor);
}

void ELogFormatter::formatLogBuffer(const ELogRecord& logRecord, ELogBuffer& logBuffer) {
    ELogBufferReceptor receptor(logBuffer);
    applyFieldSelectors(logRecord, &receptor);
}

}  // namespace elog