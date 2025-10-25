#ifndef __ELOG_RPC_FORMATTER_H__
#define __ELOG_RPC_FORMATTER_H__

#include "elog_formatter.h"

namespace elog {

class ELOG_API ELogRpcFormatter : public ELogFormatter {
public:
    ELogRpcFormatter() : ELogFormatter(TYPE_NAME), m_lastFieldType(FieldType::FT_NONE) {}
    ELogRpcFormatter(const ELogRpcFormatter&) = delete;
    ELogRpcFormatter(ELogRpcFormatter&&) = delete;
    ELogRpcFormatter& operator=(const ELogRpcFormatter&) = delete;

    static constexpr const char* TYPE_NAME = "msg";

    inline bool parseParams(const std::string& params) { return initialize(params.c_str()); }

    inline void fillInParams(const ELogRecord& logRecord, ELogFieldReceptor* receptor) {
        applyFieldSelectors(logRecord, receptor);
    }

protected:
    bool handleText(const std::string& text) override;

    bool handleField(const ELogFieldSpec& fieldSpec) override;

private:
    enum class FieldType : uint32_t { FT_NONE, FT_COMMA, FT_FIELD };
    FieldType m_lastFieldType;

    ELOG_DECLARE_LOG_FORMATTER(ELogRpcFormatter, rpc, ELOG_API)
};

}  // namespace elog

#endif  // __ELOG_RPC_FORMATTER_H__