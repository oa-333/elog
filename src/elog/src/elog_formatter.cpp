#include "elog_formatter.h"

#include "elog_string_stream_receptor.h"

namespace elog {

void ELogFormatter::formatLogMsg(const ELogRecord& logRecord, std::string& logMsg) {
    ELogStringStreamReceptor receptor;
    applyFieldSelectors(logRecord, &receptor);
    receptor.getFormattedLogMsg(logMsg);
}

}  // namespace elog