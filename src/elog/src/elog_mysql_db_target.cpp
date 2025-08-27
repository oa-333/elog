#include "elog_mysql_db_target.h"

#ifdef ELOG_ENABLE_MYSQL_DB_CONNECTOR

#include "elog_report.h"

namespace elog {

class ELogMySqlDbFieldReceptor : public ELogFieldReceptor {
public:
    ELogMySqlDbFieldReceptor(sql::PreparedStatement* stmt) : m_stmt(stmt), m_fieldNum(0) {}
    ELogMySqlDbFieldReceptor(const ELogMySqlDbFieldReceptor&) = delete;
    ELogMySqlDbFieldReceptor(ELogMySqlDbFieldReceptor&&) = delete;
    ELogMySqlDbFieldReceptor& operator=(const ELogMySqlDbFieldReceptor&) = delete;
    ~ELogMySqlDbFieldReceptor() final {}

    /** @brief Receives a string log record field. */
    void receiveStringField(uint32_t typeId, const char* field, const ELogFieldSpec& fieldSpec,
                            size_t length) final {
        m_stmt->setString(m_fieldNum++, field);
    }

    /** @brief Receives an integer log record field. */
    void receiveIntField(uint32_t typeId, uint64_t field, const ELogFieldSpec& fieldSpec) final {
        m_stmt->setInt64(m_fieldNum++, (int64_t)field);
    }

    /** @brief Receives a time log record field. */
    void receiveTimeField(uint32_t typeId, const ELogTime& logTime, const char* timeStr,
                          const ELogFieldSpec& fieldSpec, size_t length) final {
        m_stmt->setDateTime(m_fieldNum++, timeStr);
    }

    /** @brief Receives a log level log record field. */
    void receiveLogLevelField(uint32_t typeId, ELogLevel logLevel,
                              const ELogFieldSpec& fieldSpec) final {
        m_stmt->setString(m_fieldNum++, elogLevelToStr(logLevel));
    }

private:
    std::shared_ptr<sql::PreparedStatement> m_stmt;
    uint32_t m_fieldNum;
};

bool ELogMySqlDbTarget::connectDb(void* dbData) {
    MySQLDbData* mysqlDbData = validateConnectionState(dbData, false);
    if (mysqlDbData == nullptr) {
        return false;
    }

    // connect to database
    try {
        sql::Driver* driver = sql::mysql::get_driver_instance();
        mysqlDbData->m_connection.reset(driver->connect(m_url, m_user, m_passwd));
        mysqlDbData->m_connection->setSchema(m_db);

        // we need to replace every log field reference with a question mark and then prepare a
        // field selector
        const std::string& processedInsertStmt = getProcessedInsertStatement();
        mysqlDbData->m_insertStmt.reset(
            mysqlDbData->m_connection->prepareStatement(processedInsertStmt.c_str()));
    } catch (sql::SQLException& e) {
        ELOG_REPORT_ERROR(
            "Failed to start MySQL log target. SQL State: %p. Vendor Code: %d. Reason: %p",
            e.getSQLStateCStr(), e.getErrorCode(), e.what());
        mysqlDbData->m_insertStmt.reset();
        mysqlDbData->m_connection.reset();
        return false;
    }
    return true;
}

bool ELogMySqlDbTarget::disconnectDb(void* dbData) {
    MySQLDbData* mysqlDbData = validateConnectionState(dbData, true);
    if (mysqlDbData == nullptr) {
        return false;
    }

    try {
        mysqlDbData->m_insertStmt.reset();
        mysqlDbData->m_connection.reset();
    } catch (sql::SQLException& e) {
        ELOG_REPORT_ERROR("Failed to stop MySQL log target: %s", e.what());
        return false;
    }
    return true;
}

bool ELogMySqlDbTarget::execInsert(const ELogRecord& logRecord, void* dbData) {
    MySQLDbData* mysqlDbData = validateConnectionState(dbData, true);
    if (mysqlDbData == nullptr) {
        return false;
    }

    try {
        // this puts each log record field into the correct place in the prepared statement
        ELogMySqlDbFieldReceptor mySqlFieldReceptor(mysqlDbData->m_insertStmt.get());
        mysqlDbData->m_insertStmt->clearParameters();
        fillInsertStatement(logRecord, &mySqlFieldReceptor);
        if (mysqlDbData->m_insertStmt->execute()) {
            return true;
        }
        ELOG_REPORT_ERROR("Failed to send log message to MySQL log target");
    } catch (sql::SQLException& e) {
        ELOG_REPORT_ERROR("Failed to send log message to MySQL log target: %s", e.what());
    }
    return false;
}

#if 0
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
        ELOG_REPORT_ERROR(
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
        ELOG_REPORT_ERROR("Failed to stop MySQL log target: %s", e.what());
        return false;
    }
}

void ELogMySqlDbTarget::log(const ELogRecord& logRecord) {
    if (!canLog(logRecord)) {
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
        ELOG_REPORT_ERROR("Failed to send log message to MySQL log target");
    } catch (sql::SQLException& e) {
        ELOG_REPORT_ERROR("Failed to send log message to MySQL log target: %s", e.what());
    }

    // failure to send a record, so order parent class to start reconnect background task
    startReconnect();
}
#endif

ELogMySqlDbTarget::MySQLDbData* ELogMySqlDbTarget::validateConnectionState(void* dbData,
                                                                           bool shouldBeConnected) {
    if (dbData == nullptr) {
        ELOG_REPORT_ERROR(
            "Cannot connect to MySQL database, invalid connection state (internal error, database "
            "object is null)");
        return nullptr;
    }
    MySQLDbData* mysqlDbData = (MySQLDbData*)dbData;
    if (shouldBeConnected && mysqlDbData->m_connection.get() == nullptr) {
        ELOG_REPORT_ERROR(
            "Cannot connect to MySQL database, invalid connection state (internal error, "
            "connection object is null)");
        return nullptr;
    } else if (!shouldBeConnected && mysqlDbData->m_connection.get() != nullptr) {
        ELOG_REPORT_ERROR(
            "Cannot connect to MySQL database, invalid connection state (internal error, "
            "connection object is not null)");
        return nullptr;
    }
    if ((mysqlDbData->m_connection.get() == nullptr &&
         mysqlDbData->m_insertStmt.get() != nullptr) ||
        (mysqlDbData->m_connection.get() != nullptr &&
         mysqlDbData->m_insertStmt.get() == nullptr)) {
        ELOG_REPORT_ERROR(
            "Cannot connect to MySQL database, inconsistent connection state (internal error, "
            "connection and statement objects are neither both null nor both non-null)");
        return nullptr;
    }
    return mysqlDbData;
}

}  // namespace elog

#endif  // ELOG_ENABLE_MYSQL_DB_CONNECTOR