#include "elog_mysql_db_handler.h"

#ifdef ELOG_ENABLE_MYSQL_DB_CONNECTOR

#include <mysql/jdbc.h>

#include "elog_db_target.h"
#include "elog_system.h"

namespace elog {

class ELogMySqlDbFieldReceptor : public ELogFieldReceptor {
public:
    ELogMySqlDbFieldReceptor(sql::PreparedStatement* stmt) : m_stmt(stmt), m_fieldNum(0) {}
    ~ELogMySqlDbFieldReceptor() final {}

    /** @brief Receives a string log record field. */
    void receiveStringField(const std::string& field, int justify) {
        m_stmt->setString(m_fieldNum++, field);
    }

    /** @brief Receives an integer log record field. */
    void receiveIntField(uint64_t field, int justify) final {
        m_stmt->setInt64(m_fieldNum++, field);
    }

#ifdef ELOG_MSVC
    /** @brief Receives a time log record field. */
    void receiveTimeField(const SYSTEMTIME& sysTime, const char* timeStr, int justify) final {
        m_stmt->setDateTime(m_fieldNum++, timeStr);
    }
#else
    /** @brief Receives a time log record field. */
    void receiveTimeField(const timeval& sysTime, const char* timeStr, int justify) final {
        m_stmt->setDateTime(m_fieldNum++, timeStr);
    }
#endif

    /** @brief Receives a log level log record field. */
    void receiveLogLevelField(ELogLevel logLevel, int justify) final {
        m_stmt->setString(m_fieldNum++, elogLevelToStr(logLevel));
    }

private:
    std::shared_ptr<sql::PreparedStatement> m_stmt;
    uint32_t m_fieldNum;
};

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

ELogTarget* ELogMySqlDbHandler::loadTarget(const std::string& logTargetCfg,
                                           const ELogTargetSpec& targetSpec,
                                           const std::string& connString,
                                           const std::string& insertQuery) {
    // we expect 3 properties: db, user, password (optional)
    ELogPropertyMap::const_iterator itr = targetSpec.m_props.find("db");
    if (itr == targetSpec.m_props.end()) {
        ELogSystem::reportError(
            "Invalid mysql database log target specification, missing property db: %s",
            logTargetCfg.c_str());
        return nullptr;
    }
    const std::string& db = itr->second;

    itr = targetSpec.m_props.find("user");
    if (itr == targetSpec.m_props.end()) {
        ELogSystem::reportError(
            "Invalid mysql database log target specification, missing property user: %s",
            logTargetCfg.c_str());
        return nullptr;
    }
    const std::string& user = itr->second;

    itr = targetSpec.m_props.find("passwd");
    if (itr == targetSpec.m_props.end()) {
        ELogSystem::reportError(
            "Invalid mysql database log target specification, missing property passwd: %s",
            logTargetCfg.c_str());
        return nullptr;
    }
    const std::string& passwd = itr->second;
    return new (std::nothrow) ELogMySqlDbTarget(connString, db, user, passwd, insertQuery);
}

bool ELogMySqlDbTarget::start() {
    // parse the statement with log record field selector tokens
    // this builds a processed statement text with questions marks instead of log record field
    // references, and also prepares the field selector array
    if (!parseInsertStatement(m_insertStmtText)) {
        return false;
    }
    std::string processedInsertStmt = getProcessedInsertStatement();
    fprintf(stderr, "Processed insert statement: %s\n", processedInsertStmt.c_str());

    try {
        sql::Driver* driver = sql::mysql::get_driver_instance();
        fprintf(stderr, "Connecting to url %s with user/pass %s/%s\n", m_url.c_str(),
                m_user.c_str(), m_passwd.c_str());
        m_connection.reset(driver->connect(m_url, m_user, m_passwd));
        m_connection->setSchema(m_db);

        // we need to replace every log field reference with a question mark and then prepare a
        // field selector
        m_insertStmt.reset(m_connection->prepareStatement(processedInsertStmt.c_str()));
        return true;
    } catch (sql::SQLException& e) {
        ELogSystem::reportError(
            "Failed to start MySQL log target. SQL State: %p. Vendor Code: %d. Reason: %p",
            e.getSQLStateCStr(), e.getErrorCode(), e.what());
        return false;
    }
}

bool ELogMySqlDbTarget::stop() {
    try {
        m_insertStmt.reset();
        m_connection.reset();
        return true;
    } catch (sql::SQLException& e) {
        ELogSystem::reportError("Failed to stop MySQL log target: %s", e.what());
        return false;
    }
}

void ELogMySqlDbTarget::log(const ELogRecord& logRecord) {
    // we need something like field selectors here
    try {
        // this puts each log record field into the correct place in the prepared statement
        ELogMySqlDbFieldReceptor mySqlFieldReceptor(m_insertStmt.get());
        m_insertStmt->clearParameters();
        fillInsertStatement(logRecord, &mySqlFieldReceptor);
        if (!m_insertStmt->execute()) {
            ELogSystem::reportError("Failed to send log message to MySQL log target");
        }
    } catch (sql::SQLException& e) {
        ELogSystem::reportError("Failed to send log message to MySQL log target: %s", e.what());
    }
}

}  // namespace elog

#endif  // ELOG_ENABLE_MYSQL_DB_CONNECTOR
