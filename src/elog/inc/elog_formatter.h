#ifndef __ELOG_FORMATTER_H__
#define __ELOG_FORMATTER_H__

#include "elog_base_formatter.h"
#include "elog_buffer.h"

namespace elog {

// TODO: remove when integration is done
#if 0
class ELOG_API ELogFormatter final : public ELogBaseFormatter {
public:
    ELogFormatter() {}
    ELogFormatter(const ELogFormatter&) = delete;
    ELogFormatter(ELogFormatter&&) = delete;
    ELogFormatter& operator=(const ELogFormatter&) = delete;
    ~ELogFormatter() final {}

    // TODO: move these two members to ELogBaseFormatter, then get rid of this class, and rename
    // ELogBaseFormatter to ELogFormatter, and then we can fully implement the class factory
    virtual void formatLogMsg(const ELogRecord& logRecord, std::string& logMsg);

    virtual void formatLogBuffer(const ELogRecord& logRecord, ELogBuffer& logBuffer);
};
#endif

}  // namespace elog

#endif  // __ELOG_FORMATTER_H__