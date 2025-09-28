#ifndef __ELOG_DB_FORMATTER_H__
#define __ELOG_DB_FORMATTER_H__

#include "elog_base_formatter.h"

namespace elog {

class ELOG_API ELogDbFormatter : public ELogFormatter {
public:
    /** @enum Prepared statement processing style. */
    enum class QueryStyle : uint32_t {
        /** @var Specifies to replace each log record field reference token with a question mark. */
        QS_QMARK,

        /** @var Specifies to replace each log record field reference token with a dollar sign and
           ordinal field number. */
        QS_DOLLAR_ORDINAL
    };

    /** @enum Constants for prepared query parameter types (generic). */
    enum class ParamType : uint32_t {
        /** @var Parameter type is string (text). */
        PT_TEXT,

        /** @var Parameter type is integer (64 bit). */
        PT_INT,

        /** @var Parameter type is date-time (can be stored as string though). */
        PT_DATETIME,

        /** @var Parameter type is log-level (can be stored as string though). */
        PT_LOG_LEVEL
    };

    ELogDbFormatter(QueryStyle queryStyle = QueryStyle::QS_QMARK)
        : m_queryStyle(queryStyle), m_fieldNum(1) {}

    ELogDbFormatter(const ELogDbFormatter&) = delete;
    ELogDbFormatter(ELogDbFormatter&&) = delete;
    ELogDbFormatter& operator=(const ELogDbFormatter&) = delete;
    ~ELogDbFormatter() final {}

    inline void setQueryStyle(QueryStyle queryStyle) { m_queryStyle = queryStyle; }

    inline const std::string& getProcessedStatement() const { return m_processedStatement; }

    inline void fillInsertStatement(const ELogRecord& logRecord,
                                    elog::ELogFieldReceptor* receptor) {
        applyFieldSelectors(logRecord, receptor);
    }

    void getParamTypes(std::vector<ParamType>& paramTypes) const;

protected:
    bool handleText(const std::string& text) override;

    bool handleField(const ELogFieldSpec& fieldSpec) override;

private:
    QueryStyle m_queryStyle;
    uint32_t m_fieldNum;
    std::string m_processedStatement;

    ELOG_DECLARE_LOG_FORMATTER(ELogDbFormatter, db)
};

}  // namespace elog

#endif  // __ELOG_DB_FORMATTER_H__