#ifndef __ELOG_MYSQL_DB_TARGET_H__
#define __ELOG_MYSQL_DB_TARGET_H__

#ifdef ELOG_ENABLE_MYSQL_DB_CONNECTOR

// disable some annoying warnings in MySQL jdbc driver headers
#ifdef ELOG_MSVC
#pragma warning(push)
#pragma warning(disable : 4464 4626 5267 4242)
#endif

#include <mysql/jdbc.h>

#ifdef ELOG_MSVC
#pragma warning(pop)
#endif

#include <memory>

#include "elog_db_target.h"

namespace elog {

class ELOG_API ELogMySqlDbTarget : public ELogDbTarget {
public:
    ELogMySqlDbTarget(const std::string& url, const std::string& db, const std::string& user,
                      const std::string& passwd, const std::string& insertStmt,
                      ELogDbTarget::ThreadModel threadModel,
                      uint32_t maxThreads = ELOG_DB_MAX_THREADS,
                      uint32_t reconnectTimeoutMillis = ELOG_DB_RECONNECT_TIMEOUT_MILLIS)
        : ELogDbTarget("MySQL", insertStmt.c_str(), ELogDbFormatter::QueryStyle::QS_QMARK,
                       threadModel, maxThreads, reconnectTimeoutMillis),
          m_url(url),
          m_db(db),
          m_user(user),
          m_passwd(passwd),
          m_insertStmtText(insertStmt) {}

    ELogMySqlDbTarget(const ELogMySqlDbTarget&) = delete;
    ELogMySqlDbTarget(ELogMySqlDbTarget&&) = delete;
    ELogMySqlDbTarget& operator=(const ELogMySqlDbTarget&) = delete;
    ~ELogMySqlDbTarget() final {}

protected:
    /** @brief Allocates database access object. */
    void* allocDbData() final { return new (std::nothrow) MySQLDbData(); }

    /** @brief Frees database access object. */
    void freeDbData(void* dbData) final { delete ((MySQLDbData*)dbData); }

    /** @brief Initializes database access object. */
    bool connectDb(void* dbData) final;

    bool disconnectDb(void* dbData) final;

    /** @brief Sends a log record to a log target. */
    bool execInsert(const ELogRecord& logRecord, void* dbData) final;

private:
    std::string m_url;
    std::string m_db;
    std::string m_user;
    std::string m_passwd;
    std::string m_insertStmtText;

    struct MySQLDbData {
        MySQLDbData() {}
        MySQLDbData(const MySQLDbData&) = delete;
        MySQLDbData(MySQLDbData&&) = delete;
        MySQLDbData& operator=(const MySQLDbData&) = delete;
        ~MySQLDbData() {}

        std::unique_ptr<sql::Connection> m_connection;
        std::unique_ptr<sql::PreparedStatement> m_insertStmt;
    };

    MySQLDbData* validateConnectionState(void* dbData, bool shouldBeConnected);
};

}  // namespace elog

#endif  // ELOG_ENABLE_MYSQL_DB_CONNECTOR

#endif  // __ELOG_MYSQL_DB_TARGET_H__