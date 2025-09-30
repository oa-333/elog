#ifndef __ELOG_MSG_FORMATTER_H__
#define __ELOG_MSG_FORMATTER_H__

#include "elog_base_formatter.h"

namespace elog {

class ELOG_API ELogMsgFormatter : public ELogFormatter {
public:
    ELogMsgFormatter() : m_lastFieldType(FieldType::FT_NONE) {}
    ELogMsgFormatter(const ELogMsgFormatter&) = delete;
    ELogMsgFormatter(ELogMsgFormatter&&) = delete;
    ELogMsgFormatter& operator=(const ELogMsgFormatter&) = delete;
    ~ELogMsgFormatter() final {}

    inline bool parseParams(const std::string& params) { return initialize(params.c_str()); }

    inline void fillInParams(const ELogRecord& logRecord, elog::ELogFieldReceptor* receptor) {
        applyFieldSelectors(logRecord, receptor);
    }

protected:
    bool handleText(const std::string& text) override;

    bool handleField(const ELogFieldSpec& fieldSpec) override;

private:
    enum class FieldType : uint32_t { FT_NONE, FT_COMMA, FT_FIELD };
    FieldType m_lastFieldType;

    ELOG_DECLARE_LOG_FORMATTER(ELogMsgFormatter, msg)
};

}  // namespace elog

#endif  // __ELOG_MSG_FORMATTER_H__