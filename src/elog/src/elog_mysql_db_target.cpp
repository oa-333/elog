#include "elog_mysql_db_target.h"

#ifdef ELOG_ENABLE_MYSQL_DB_CONNECTOR

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

bool ELogMySqlDbTarget::start() {
    // parse the statement with log record field selector tokens
    // this builds a processed statement text with questions marks instead of log record field
    // references, and also prepares the field selector array
    if (!parseInsertStatement(m_insertStmtText)) {
        return false;
    }
    std::string processedInsertStmt = getProcessedInsertStatement();

    try {
        sql::Driver* driver = sql::mysql::get_driver_instance();
        m_connection.reset(driver->connect(m_url, m_user, m_passwd));
        m_connection->setSchema(m_db);

        // we need to replace every log field reference with a question mark and then prepare a
        // field selector
        m_insertStmt.reset(m_connection->prepareStatement(processedInsertStmt.c_str()));
    } catch (sql::SQLException& e) {
        ELogSystem::reportError(
            "Failed to start MySQL log target. SQL State: %p. Vendor Code: %d. Reason: %p",
            e.getSQLStateCStr(), e.getErrorCode(), e.what());
        m_insertStmt.reset();
        m_connection.reset();
        return false;
    }

    // notify parent class about connection state
    setConnected();
    return true;
}

bool ELogMySqlDbTarget::stop() {
    // stop any reconnect background task
    stopReconnect();

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
    if (!shouldLog(logRecord)) {
        return;
    }

    // check if connected to database, otherwise discard log record
    if (!isConnected()) {
        return;
    }

    try {
        // this puts each log record field into the correct place in the prepared statement
        ELogMySqlDbFieldReceptor mySqlFieldReceptor(m_insertStmt.get());
        m_insertStmt->clearParameters();
        fillInsertStatement(logRecord, &mySqlFieldReceptor);
        if (m_insertStmt->execute()) {
            return;
        }
        ELogSystem::reportError("Failed to send log message to MySQL log target");
    } catch (sql::SQLException& e) {
        ELogSystem::reportError("Failed to send log message to MySQL log target: %s", e.what());
    }

    // failure to send a record, so order parent class to start reconnect background task
    startReconnect();
}

}  // namespace elog

#endif  // ELOG_ENABLE_MYSQL_DB_CONNECTOR