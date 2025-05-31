#ifndef __ELOG_RPC_FORMATTER_H__
#define __ELOG_RPC_FORMATTER_H__

#include "elog_base_formatter.h"

namespace elog {

class ELOG_API ELogRpcFormatter : public ELogBaseFormatter {
public:
    ELogRpcFormatter() : m_lastFieldType(FieldType::FT_NONE) {}
    ELogRpcFormatter(const ELogRpcFormatter&) = delete;
    ELogRpcFormatter(ELogRpcFormatter&&) = delete;
    ~ELogRpcFormatter() final {}

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
};

}  // namespace elog

#endif  // __ELOG_RPC_FORMATTER_H__