#ifndef __ELOG_DB_TARGET_H__
#define __ELOG_DB_TARGET_H__

#include "elog_db_formatter.h"
#include "elog_target.h"

namespace elog {

/** @brief Abstract parent class for DB log targets. */
class ELogDbTarget : public ELogTarget {
public:
    /** @brief Orders a buffered log target to flush it log messages. */
    void flush() final {}

protected:
    ELogDbTarget(ELogDbFormatter::QueryStyle queryStyle) : m_formatter(queryStyle) {}
    ~ELogDbTarget() override {}

    /**
     * @brief Parses the insert statement loaded from configuration, builds all log record field
     * selectors, and transforms the insert statement into DB acceptable format (i.e. with questions
     * marks as place-holders or dollar sign with parameter ordinal number).
     * @param insertStatement The insert statement to parse.
     * @return true If succeeded, otherwise false.
     */
    bool parseInsertStatement(const std::string& insertStatement);

    /**
     * @brief Retrieves the processed insert statement resulting from the call to @ref
     * parseInsertStatement().
     */
    inline const std::string& getProcessedInsertStatement() const {
        return m_formatter.getProcessedStatement();
    }

    /**
     * @brief Applies all field selectors to the given log record, so that all prepared statement
     * parameters are filled.
     * @param logRecord The log record to process.
     * @param receptor The receptor that receives log record fields and transfers them to the
     * prepared statement parameters. The receptor 
     */
    inline void fillInsertStatement(const elog::ELogRecord& logRecord,
                                    elog::ELogFieldReceptor* receptor) {
        m_formatter.fillInsertStatement(logRecord, receptor);
    }

private:
    ELogDbFormatter m_formatter;
    std::string m_processedInsertQuery;
};

}  // namespace elog

#endif  // __ELOG_DB_TARGET_H__