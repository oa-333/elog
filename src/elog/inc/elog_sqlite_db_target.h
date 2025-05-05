#ifndef __ELOG_SQLITE_DB_TARGET_H__
#define __ELOG_SQLITE_DB_TARGET_H__

#include "elog_def.h"

#ifdef ELOG_ENABLE_SQLITE_DB_CONNECTOR

#include <sqlite3.h>

#include <memory>

#include "elog_db_target.h"

namespace elog {

class ELogSQLiteDbTarget : public ELogDbTarget {
public:
    ELogSQLiteDbTarget(const std::string& filePath, const std::string& insertStmt)
        : ELogDbTarget(ELogDbFormatter::QueryStyle::QS_QMARK),
          m_filePath(filePath),
          m_insertStmtText(insertStmt),
          m_connection(nullptr),
          m_insertStmt(nullptr) {}

    ELogSQLiteDbTarget(const ELogSQLiteDbTarget&) = delete;
    ELogSQLiteDbTarget(ELogSQLiteDbTarget&&) = delete;
    ~ELogSQLiteDbTarget() final {}

    /** @brief Order the log target to start (required for threaded targets). */
    bool start() final;

    /** @brief Order the log target to stop (required for threaded targets). */
    bool stop() final;

    /** @brief Sends a log record to a log target. */
    void log(const ELogRecord& logRecord) final;

private:
    std::string m_filePath;
    std::string m_insertStmtText;

    sqlite3* m_connection;
    sqlite3_stmt* m_insertStmt;
};

}  // namespace elog

#endif  // ELOG_ENABLE_MYSQL_DB_CONNECTOR

#endif  // __ELOG_SQLITE_DB_TARGET_H__