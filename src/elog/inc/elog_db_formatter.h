#ifndef __ELOG_DB_FORMATTER_H__
#define __ELOG_DB_FORMATTER_H__

#include "elog_formatter.h"

namespace elog {

class ELogDbFormatter : public ELogBaseFormatter {
public:
    /** @enum Prepared statement processing style. */
    enum class QueryStyle : uint32_t {
        /** @var Specifies to replace each log record field reference token with a question mark. */
        QS_QMARK,

        /** @var Specifies to replace each log record field reference token with a dollar sign and
           ordinal field number. */
        QS_DOLLAR_ORDINAL
    };

    ELogDbFormatter(QueryStyle queryStyle) : m_queryStyle(queryStyle), m_fieldNum(1) {}

    ELogDbFormatter(const ELogDbFormatter&) = delete;
    ELogDbFormatter(ELogDbFormatter&&) = delete;
    ~ELogDbFormatter() final {}

    inline const std::string& getProcessedStatement() const { return m_processedStatement; }

    inline void formatInsertStatement(const ELogRecord& logRecord,
                                      elog::ELogFieldReceptor* receptor) {
        applyFieldSelectors(logRecord, receptor);
    }

protected:
    void handleText(const std::string& text) override;

    bool handleField(const char* fieldName, int justify) override;

private:
    QueryStyle m_queryStyle;
    std::string m_processedStatement;
    uint32_t m_fieldNum;
};

}  // namespace elog

#endif  // __ELOG_DB_FORMATTER_H__