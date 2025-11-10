#ifndef __ELOG_MYSQL_DB_TARGET_H__
#define __ELOG_MYSQL_DB_TARGET_H__

#ifdef ELOG_ENABLE_MYSQL_DB_CONNECTOR

// disable some annoying warnings in MySQL jdbc driver headers
#ifdef ELOG_MSVC
#pragma warning(push)
#pragma warning(disable : 4464 4626 5267 4242)
#endif

// compiler complains this is not defined, so we set it to 1
// TODO: find out more about
#define CONCPP_BUILD_SHARED 1
#include <mysql/jdbc.h>

#ifdef ELOG_MSVC
#pragma warning(pop)
#endif

#include <memory>

#include "elog_db_target.h"

namespace elog {

class ELOG_API ELogMySqlDbTarget : public ELogDbTarget {
public:
    // if maxThreads is zero, then the number configured during elog::initialize() will be used
    // the user is allowed here to override the value specified during elog::initialize()
    ELogMySqlDbTarget(const ELogDbConfig& dbConfig, const std::string& db, const std::string& user,
                      const std::string& passwd)
        : ELogDbTarget("MySQL", dbConfig, ELogDbFormatter::QueryStyle::QS_QMARK),
          m_url(dbConfig.m_connString),
          m_db(db),
          m_user(user),
          m_passwd(passwd),
          m_insertStmtText(dbConfig.m_insertQuery) {}

    ELogMySqlDbTarget(const ELogMySqlDbTarget&) = delete;
    ELogMySqlDbTarget(ELogMySqlDbTarget&&) = delete;
    ELogMySqlDbTarget& operator=(const ELogMySqlDbTarget&) = delete;

    ELOG_DECLARE_LOG_TARGET(ELogMySqlDbTarget)

protected:
    /** @brief Allocates database access object. */
    void* allocDbData() final { return new (std::nothrow) MySQLDbData(); }

    /** @brief Frees database access object. */
    void freeDbData(void* dbData) final { delete ((MySQLDbData*)dbData); }

    /** @brief Initializes database access object. */
    bool connectDb(void* dbData) final;

    bool disconnectDb(void* dbData) final;

    /** @brief Sends a log record to a log target. */
    bool execInsert(const ELogRecord& logRecord, void* dbData, uint64_t& bytesWritten) final;

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