#ifndef __ELOG_PGSQL_DB_TARGET_H__
#define __ELOG_PGSQL_DB_TARGET_H__

#ifdef ELOG_ENABLE_PGSQL_DB_CONNECTOR

#include <libpq-fe.h>

#include "elog_db_target.h"

namespace elog {

class ELOG_API ELogPGSQLDbTarget : public ELogDbTarget {
public:
    ELogPGSQLDbTarget(const std::string& host, uint32_t port, const std::string& db,
                      const std::string& user, const std::string& passwd,
                      const std::string& insertStmt, ELogDbTarget::ThreadModel threadModel,
                      uint32_t maxThreads = ELOG_DB_MAX_THREADS,
                      uint32_t reconnectTimeoutMillis = ELOG_DB_RECONNECT_TIMEOUT_MILLIS)
        : ELogDbTarget("PostgreSQL", insertStmt.c_str(),
                       ELogDbFormatter::QueryStyle::QS_DOLLAR_ORDINAL, threadModel, maxThreads,
                       reconnectTimeoutMillis) {
        formatConnString(host, port, db, user, passwd);
    }

    ELogPGSQLDbTarget(const ELogPGSQLDbTarget&) = delete;
    ELogPGSQLDbTarget(ELogPGSQLDbTarget&&) = delete;
    ~ELogPGSQLDbTarget() final {}

protected:
    void initDbTarget() final;

    /** @brief Allocates database access object. */
    void* allocDbData() final { return new (std::nothrow) PGSQLDbData(); }

    /** @brief Frees database access object. */
    void freeDbData(void* dbData) final { delete ((PGSQLDbData*)dbData); }

    /** @brief Initializes database access object. */
    bool connectDb(void* dbData) final;

    bool disconnectDb(void* dbData) final;

    /** @brief Sends a log record to a log target. */
    bool execInsert(const ELogRecord& logRecord, void* dbData) final;

private:
    std::string m_connString;
    std::string m_stmtName;
    std::vector<Oid> m_pgParamTypes;
    std::vector<int> m_paramFormats;

    struct PGSQLDbData {
        PGconn* m_conn;
        PGSQLDbData() : m_conn(nullptr) {}
    };

    // void convertToPgParamTypes();
    void formatConnString(const std::string& host, uint32_t port, const std::string& db,
                          const std::string& user, const std::string& passwd);

    PGSQLDbData* validateConnectionState(void* dbData, bool shouldBeConnected);
};

}  // namespace elog

#endif  // ELOG_ENABLE_PGSQL_DB_CONNECTOR

#endif  // __ELOG_PGSQL_DB_TARGET_H__