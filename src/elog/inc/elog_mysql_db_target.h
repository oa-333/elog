#ifndef __ELOG_MYSQL_DB_TARGET_H__
#define __ELOG_MYSQL_DB_TARGET_H__

#include "elog_def.h"

#ifdef ELOG_MSVC
#define ELOG_ENABLE_MYSQL_DB_CONNECTOR
#endif

#ifdef ELOG_ENABLE_MYSQL_DB_CONNECTOR

#include <mysql/jdbc.h>

#include <memory>

#include "elog_db_target.h"

namespace elog {

class ELogMySqlDbTarget : public ELogDbTarget {
public:
    ELogMySqlDbTarget(const std::string& url, const std::string& db, const std::string& user,
                      const std::string& passwd, const std::string& insertStmt)
        : ELogDbTarget(ELogDbFormatter::QueryStyle::QS_QMARK),
          m_url(url),
          m_db(db),
          m_user(user),
          m_passwd(passwd),
          m_insertStmtText(insertStmt) {}

    ELogMySqlDbTarget(const ELogMySqlDbTarget&) = delete;
    ELogMySqlDbTarget(ELogMySqlDbTarget&&) = delete;
    ~ELogMySqlDbTarget() final {}

    /** @brief Order the log target to start (required for threaded targets). */
    bool start() final;

    /** @brief Order the log target to stop (required for threaded targets). */
    bool stop() final;

    /** @brief Sends a log record to a log target. */
    void log(const ELogRecord& logRecord) final;

private:
    std::string m_url;
    std::string m_db;
    std::string m_user;
    std::string m_passwd;
    std::string m_insertStmtText;

    std::unique_ptr<sql::Connection> m_connection;
    std::unique_ptr<sql::PreparedStatement> m_insertStmt;
};

}  // namespace elog

#endif  // ELOG_ENABLE_MYSQL_DB_CONNECTOR

#endif  // __ELOG_MYSQL_DB_TARGET_H__