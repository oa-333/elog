#include "elog_string_formatter.h"

#include "elog_string_stream_receptor.h"

namespace elog {

static thread_local ELogStringStreamReceptor sReceptor;

void ELogFormatter::formatLogMsg(const ELogRecord& logRecord, std::string& logMsg) {
    ELogStringStreamReceptor receptor;
    applyFieldSelectors(logRecord, &receptor);
    logMsg = receptor.getFormattedLogMsg();
}

}  // namespace elog