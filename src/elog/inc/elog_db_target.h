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

    bool parseInsertStatement(const std::string& insertStatement);

    inline const std::string& getProcessedInsertStatement() const {
        return m_formatter.getProcessedStatement();
    }

    inline void formatInsertStatement(const elog::ELogRecord& logRecord,
                                      elog::ELogFieldReceptor* receptor) {
        m_formatter.formatInsertStatement(logRecord, receptor);
    }

private:
    ELogDbFormatter m_formatter;
    std::string m_processedInsertQuery;
};

}  // namespace elog

#endif  // __ELOG_DB_TARGET_H__