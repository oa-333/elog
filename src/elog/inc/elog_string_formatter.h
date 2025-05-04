#ifndef __ELOG_STRING_FORMATTER_H__
#define __ELOG_STRING_FORMATTER_H__

#include "elog_base_formatter.h"

namespace elog {

class ELOG_API ELogFormatter : public ELogBaseFormatter {
public:
    ELogFormatter() {}
    ~ELogFormatter() final {}

    void formatLogMsg(const ELogRecord& logRecord, std::string& logMsg);
};

}  // namespace elog

#endif  // __ELOG_STRING_FORMATTER_H__