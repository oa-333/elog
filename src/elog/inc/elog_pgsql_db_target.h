#ifndef __ELOG_PGSQL_DB_TARGET_H__
#define __ELOG_PGSQL_DB_TARGET_H__

#include "elog_def.h"

#ifdef ELOG_ENABLE_PGSQL_DB_CONNECTOR

#include <libpq-fe.h>

#include "elog_db_target.h"

namespace elog {

class ELogPGSQLDbTarget : public ELogDbTarget {
public:
    ELogPGSQLDbTarget(const std::string& host, uint32_t port, const std::string& db,
                      const std::string& user, const std::string& passwd,
                      const std::string& insertStmt)
        : ELogDbTarget(ELogDbFormatter::QueryStyle::QS_DOLLAR_ORDINAL),
          m_insertStmtText(insertStmt),
          m_insertStatementParsed(false),
          m_connection(nullptr) {
        formatConnString(host, port, db, user, passwd);
    }

    ELogPGSQLDbTarget(const ELogPGSQLDbTarget&) = delete;
    ELogPGSQLDbTarget(ELogPGSQLDbTarget&&) = delete;
    ~ELogPGSQLDbTarget() final {}

    /** @brief Order the log target to start (required for threaded targets). */
    bool start() final;

    /** @brief Order the log target to stop (required for threaded targets). */
    bool stop() final;

    /** @brief Sends a log record to a log target. */
    void log(const ELogRecord& logRecord) final;

private:
    std::string m_connString;
    std::string m_insertStmtText;
    std::string m_processedInsertStmt;
    bool m_insertStatementParsed;
    std::mutex m_connLock;

    PGconn* m_connection;
    std::string m_stmtName;
    std::vector<ELogDbFormatter::ParamType> m_paramTypes;
    std::vector<Oid> m_pgParamTypes;
    std::vector<int> m_paramFormats;

    // void convertToPgParamTypes();
    void formatConnString(const std::string& host, uint32_t port, const std::string& db,
                          const std::string& user, const std::string& passwd);
};

}  // namespace elog

#endif  // ELOG_ENABLE_MYSQL_DB_CONNECTOR

#endif  // __ELOG_PGSQL_DB_TARGET_H__