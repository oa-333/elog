#ifndef __ELOG_MSGQ_FORMATTER_H__
#define __ELOG_MSGQ_FORMATTER_H__

#include "elog_base_formatter.h"

namespace elog {

class ELOG_API ELogMsgQFormatter : public ELogBaseFormatter {
public:
    ELogMsgQFormatter() : m_lastFieldType(FieldType::FT_NONE) {}
    ELogMsgQFormatter(const ELogMsgQFormatter&) = delete;
    ELogMsgQFormatter(ELogMsgQFormatter&&) = delete;
    ~ELogMsgQFormatter() final {}

    inline bool parseHeaders(const std::string& headers) { return initialize(headers.c_str()); }

    inline void fillInHeaders(const ELogRecord& logRecord, elog::ELogFieldReceptor* receptor) {
        applyFieldSelectors(logRecord, receptor);
    }

    inline const std::string& getHeaderNameAt(uint32_t index) const { return m_headerNames[index]; }

    inline uint32_t getHeaderCount() const { return m_headerNames.size(); }

    inline const std::vector<std::string>& getHeaderNames() const { return m_headerNames; }

protected:
    bool handleText(const std::string& text) override;

    bool handleField(const char* fieldName, int justify) override;

private:
    std::vector<std::string> m_headerNames;
    enum class FieldType : uint32_t { FT_NONE, FT_TEXT, FT_FIELD };
    FieldType m_lastFieldType;
};

}  // namespace elog

#endif  // __ELOG_MSGQ_FORMATTER_H__