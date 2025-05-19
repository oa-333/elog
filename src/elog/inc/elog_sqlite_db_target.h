#ifndef __ELOG_SQLITE_DB_TARGET_H__
#define __ELOG_SQLITE_DB_TARGET_H__

#ifdef ELOG_ENABLE_SQLITE_DB_CONNECTOR

#include <sqlite3.h>

#include <memory>

#include "elog_db_target.h"

namespace elog {

class ELOG_API ELogSQLiteDbTarget : public ELogDbTarget {
public:
    ELogSQLiteDbTarget(const std::string& filePath, const std::string& insertStmt,
                       ELogDbTarget::ThreadModel threadModel,
                       uint32_t maxThreads = ELOG_DB_MAX_THREADS,
                       uint32_t reconnectTimeoutMillis = ELOG_DB_RECONNECT_TIMEOUT_MILLIS)
        : ELogDbTarget("SQLite", insertStmt.c_str(), ELogDbFormatter::QueryStyle::QS_QMARK,
                       threadModel, maxThreads, reconnectTimeoutMillis),
          m_filePath(filePath),
          m_insertStmtText(insertStmt),
          m_connection(nullptr),
          m_insertStmt(nullptr) {}

    ELogSQLiteDbTarget(const ELogSQLiteDbTarget&) = delete;
    ELogSQLiteDbTarget(ELogSQLiteDbTarget&&) = delete;
    ~ELogSQLiteDbTarget() final {}

protected:
    /** @brief Allocates database access object. */
    void* allocDbData() final { return new (std::nothrow) SQLiteDbData(); }

    /** @brief Frees database access object. */
    void freeDbData(void* dbData) final { delete ((SQLiteDbData*)dbData); }

    /** @brief Initializes database access object. */
    bool connectDb(void* dbData) final;

    bool disconnectDb(void* dbData) final;

    /** @brief Sends a log record to a log target. */
    bool execInsert(const ELogRecord& logRecord, void* dbData) final;

private:
    std::string m_filePath;
    std::string m_insertStmtText;

    sqlite3* m_connection;
    sqlite3_stmt* m_insertStmt;

    struct SQLiteDbData {
        sqlite3* m_connection;
        sqlite3_stmt* m_insertStmt;
        SQLiteDbData() : m_connection(nullptr), m_insertStmt(nullptr) {}
    };

    SQLiteDbData* validateConnectionState(void* dbData, bool shouldBeConnected);
};

}  // namespace elog

#endif  // ELOG_ENABLE_MYSQL_DB_CONNECTOR

#endif  // __ELOG_SQLITE_DB_TARGET_H__