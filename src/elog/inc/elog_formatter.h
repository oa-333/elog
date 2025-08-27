#ifndef __ELOG_FORMATTER_H__
#define __ELOG_FORMATTER_H__

#include "elog_base_formatter.h"
#include "elog_buffer.h"

namespace elog {

class ELOG_API ELogFormatter final : public ELogBaseFormatter {
public:
    ELogFormatter() {}
    ELogFormatter(const ELogFormatter&) = delete;
    ELogFormatter(ELogFormatter&&) = delete;
    ELogFormatter& operator=(const ELogFormatter&) = delete;
    ~ELogFormatter() final {}

    virtual void formatLogMsg(const ELogRecord& logRecord, std::string& logMsg);

    virtual void formatLogBuffer(const ELogRecord& logRecord, ELogBuffer& logBuffer);
};

}  // namespace elog

#endif  // __ELOG_FORMATTER_H__