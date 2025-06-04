#include "elog_sqlite_db_target.h"

#ifdef ELOG_ENABLE_SQLITE_DB_CONNECTOR

#include <cstring>

#include "elog_error.h"

namespace elog {

/*static std::string iso_8859_1_to_utf8(const std::string& str) {
    std::string strOut;
    for (std::string::const_iterator it = str.begin(); it != str.end(); ++it) {
        uint8_t ch = *it;
        if (ch < 0x80) {
            strOut.push_back(ch);
        } else {
            strOut.push_back(0xc0 | ch >> 6);
            strOut.push_back(0x80 | (ch & 0x3f));
        }
    }
    return strOut;
}*/

class ELogSQLiteDbFieldReceptor : public ELogFieldReceptor {
public:
    ELogSQLiteDbFieldReceptor(sqlite3_stmt* stmt) : m_res(0), m_stmt(stmt), m_fieldNum(1) {}
    ~ELogSQLiteDbFieldReceptor() final {}

    /** @brief Retrieves last operation's result code. */
    inline int getRes() const { return m_res; }

    /** @brief Receives a string log record field. */
    void receiveStringField(uint32_t typeId, const std::string& field,
                            const ELogFieldSpec& fieldSpec) {
        int res = sqlite3_bind_text(m_stmt, m_fieldNum++, field.c_str(), field.length(),
                                    SQLITE_TRANSIENT);
        if (m_res == 0) {
            m_res = res;
        }
    }

    /** @brief Receives an integer log record field. */
    void receiveIntField(uint32_t typeId, uint64_t field, const ELogFieldSpec& fieldSpec) final {
        int res = sqlite3_bind_int64(m_stmt, m_fieldNum++, field);
        if (m_res == 0) {
            m_res = res;
        }
    }

    /** @brief Receives a time log record field. */
    void receiveTimeField(uint32_t typeId, const ELogTime& logTime, const char* timeStr,
                          const ELogFieldSpec& fieldSpec) final {
        int res = sqlite3_bind_text(m_stmt, m_fieldNum++, timeStr, -1, SQLITE_TRANSIENT);
        if (m_res == 0) {
            m_res = res;
        }
    }

    /** @brief Receives a log level log record field. */
    void receiveLogLevelField(uint32_t typeId, ELogLevel logLevel,
                              const ELogFieldSpec& fieldSpec) final {
        const char* logLevelStr = elogLevelToStr(logLevel);
        int res = sqlite3_bind_text(m_stmt, m_fieldNum++, logLevelStr, strlen(logLevelStr),
                                    SQLITE_TRANSIENT);
        if (m_res == 0) {
            m_res = res;
        }
    }

private:
    int m_res;
    sqlite3_stmt* m_stmt;
    uint32_t m_fieldNum;
};

bool ELogSQLiteDbTarget::connectDb(void* dbData) {
    SQLiteDbData* sqliteDbData = validateConnectionState(dbData, false);
    if (sqliteDbData == nullptr) {
        return false;
    }

    // connect to database
    // NOTE: SQLITE_OPEN_NOMUTEX is specified since we rely on upper layer thread model
    int res = sqlite3_open_v2(m_filePath.c_str(), &sqliteDbData->m_connection,
                              SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOMUTEX, nullptr);
    if (res != SQLITE_OK) {
        ELOG_REPORT_ERROR("Failed to open sqlite db at path %s: %s", m_filePath.c_str(),
                          sqlite3_errstr(res));
        return false;
    }
    ELOG_REPORT_TRACE("Connected to SQLite3");

    const std::string& processedInsertStatement = getProcessedInsertStatement();
    res =
        sqlite3_prepare_v2(sqliteDbData->m_connection, processedInsertStatement.c_str(),
                           processedInsertStatement.length(), &sqliteDbData->m_insertStmt, nullptr);
    if (res != SQLITE_OK) {
        ELOG_REPORT_ERROR("Failed to prepare sqlite statement '%s': %s",
                          processedInsertStatement.c_str(), sqlite3_errstr(res));
        sqlite3_close_v2(sqliteDbData->m_connection);
        sqliteDbData->m_connection = nullptr;
        return false;
    }
    ELOG_REPORT_TRACE("SQLite3 connection and prepared statement are ready");
    return true;
}

bool ELogSQLiteDbTarget::disconnectDb(void* dbData) {
    SQLiteDbData* sqliteDbData = validateConnectionState(dbData, true);
    if (sqliteDbData == nullptr) {
        return false;
    }

    if (sqliteDbData->m_insertStmt != nullptr) {
        int res = sqlite3_finalize(sqliteDbData->m_insertStmt);
        if (res != SQLITE_OK) {
            ELOG_REPORT_ERROR("Failed to destroy sqlite statement: %s", sqlite3_errstr(res));
            return false;
        }
        sqliteDbData->m_insertStmt = nullptr;
    }

    if (sqliteDbData->m_connection != nullptr) {
        int res = sqlite3_close_v2(sqliteDbData->m_connection);
        if (res != SQLITE_OK) {
            ELOG_REPORT_ERROR("Failed to close sqlite connection: %s", sqlite3_errstr(res));
            return false;
        }
        sqliteDbData->m_connection = nullptr;
    }
    return true;
}

bool ELogSQLiteDbTarget::execInsert(const ELogRecord& logRecord, void* dbData) {
    SQLiteDbData* sqliteDbData = validateConnectionState(dbData, true);
    if (sqliteDbData == nullptr) {
        return false;
    }

    // reset statement parameters
    int res = sqlite3_reset(sqliteDbData->m_insertStmt);
    if (res != SQLITE_OK) {
        ELOG_REPORT_ERROR("Failed to reset sqlite statement: %s", sqlite3_errstr(res));
        return false;
    }

    // this puts each log record field into the correct place in the prepared statement
    ELogSQLiteDbFieldReceptor sqliteFieldReceptor(sqliteDbData->m_insertStmt);
    fillInsertStatement(logRecord, &sqliteFieldReceptor);
    res = sqliteFieldReceptor.getRes();
    if (res != SQLITE_OK) {
        ELOG_REPORT_ERROR("Failed to bind sqlite statement parameters: %s", sqlite3_errstr(res));
        return false;
    }

    // execute statement, retry if busy, discard all returned data (there shouldn't be any, though)
    res = sqlite3_step(sqliteDbData->m_insertStmt);
    while (res == SQLITE_BUSY) {
        res = sqlite3_step(sqliteDbData->m_insertStmt);
    }
    while (res == SQLITE_ROW) {
        res = sqlite3_step(sqliteDbData->m_insertStmt);
    }
    if (res == SQLITE_DONE) {
        return true;
    }
    ELOG_REPORT_ERROR("Failed to execute sqlite statement parameters: %s", sqlite3_errstr(res));
    return false;
}

#if 0
bool ELogSQLiteDbTarget::start() {
    // parse the statement with log record field selector tokens
    // this builds a processed statement text with questions marks instead of log record field
    // references, and also prepares the field selector array
    if (!parseInsertStatement(m_insertStmtText)) {
        return false;
    }
    std::string processedInsertStmt = getProcessedInsertStatement();

    int res = sqlite3_open_v2(m_filePath.c_str(), &m_connection,
                              SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX, nullptr);
    if (res != SQLITE_OK) {
        ELOG_REPORT_ERROR("Failed to open sqlite db at path %s: %s", m_filePath.c_str(),
                                sqlite3_errstr(res));
        return false;
    }

    res = sqlite3_prepare_v2(m_connection, processedInsertStmt.c_str(),
                             processedInsertStmt.length(), &m_insertStmt, nullptr);
    if (res != SQLITE_OK) {
        ELOG_REPORT_ERROR("Failed to prepare sqlite statement '%s': %s",
                                processedInsertStmt.c_str(), sqlite3_errstr(res));
        sqlite3_close_v2(m_connection);
        m_connection = nullptr;
        return false;
    }

    // notify parent class about connection state
    setConnected();
    return true;
}

bool ELogSQLiteDbTarget::stop() {
    // stop any reconnect background task
    stopReconnect();

    if (m_insertStmt != nullptr) {
        int res = sqlite3_finalize(m_insertStmt);
        if (res != SQLITE_OK) {
            ELOG_REPORT_ERROR("Failed to destroy sqlite statement: %s", sqlite3_errstr(res));
            return false;
        }
        m_insertStmt = nullptr;
    }

    if (m_connection != nullptr) {
        int res = sqlite3_close_v2(m_connection);
        if (res != SQLITE_OK) {
            ELOG_REPORT_ERROR("Failed to close sqlite connection: %s", sqlite3_errstr(res));
            return false;
        }
        m_connection = nullptr;
    }
    return true;
}

void ELogSQLiteDbTarget::log(const ELogRecord& logRecord) {
    if (!shouldLog(logRecord)) {
        return;
    }

    // check if connected to database, otherwise discard log record
    if (!isConnected()) {
        return;
    }

    // reset statement parameters
    int res = sqlite3_reset(m_insertStmt);
    if (res != SQLITE_OK) {
        ELOG_REPORT_ERROR("Failed to reset sqlite statement: %s", sqlite3_errstr(res));

        // failure to send a record, so order parent class to start reconnect background task
        startReconnect();
        return;
    }

    // this puts each log record field into the correct place in the prepared statement
    ELogSQLiteDbFieldReceptor sqliteFieldReceptor(m_insertStmt);
    fillInsertStatement(logRecord, &sqliteFieldReceptor);
    res = sqliteFieldReceptor.getRes();
    if (res != SQLITE_OK) {
        ELOG_REPORT_ERROR("Failed to bind sqlite statement parameters: %s",
                                sqlite3_errstr(res));

        // failure to send a record, so order parent class to start reconnect background task
        startReconnect();
        return;
    }

    // execute statement, retry if busy, discard all returned data (there shouldn't be any, though)
    res = sqlite3_step(m_insertStmt);
    while (res == SQLITE_BUSY) {
        res = sqlite3_step(m_insertStmt);
    }
    while (res == SQLITE_ROW) {
        res = sqlite3_step(m_insertStmt);
    }
    if (res == SQLITE_DONE) {
        return;
    }
    ELOG_REPORT_ERROR("Failed to execute sqlite statement parameters: %s",
                            sqlite3_errstr(res));

    // failure to send a record, so order parent class to start reconnect background task
    startReconnect();
}
#endif

ELogSQLiteDbTarget::SQLiteDbData* ELogSQLiteDbTarget::validateConnectionState(
    void* dbData, bool shouldBeConnected) {
    if (dbData == nullptr) {
        ELOG_REPORT_ERROR(
            "Cannot connect to SQLite database, invalid connection state (internal error, database "
            "object is null)");
        return nullptr;
    }
    SQLiteDbData* sqliteDbData = (SQLiteDbData*)dbData;
    if (shouldBeConnected && sqliteDbData->m_connection == nullptr) {
        ELOG_REPORT_ERROR(
            "Cannot connect to SQLite database, invalid connection state (internal error, "
            "connection object is null)");
        return nullptr;
    } else if (!shouldBeConnected && sqliteDbData->m_connection != nullptr) {
        ELOG_REPORT_ERROR(
            "Cannot connect to SQLite database, invalid connection state (internal error, "
            "connection object is not null)");
        return nullptr;
    }
    if ((sqliteDbData->m_connection == nullptr && sqliteDbData->m_insertStmt != nullptr) ||
        (sqliteDbData->m_connection != nullptr && sqliteDbData->m_insertStmt == nullptr)) {
        ELOG_REPORT_ERROR(
            "Cannot connect to SQLite database, inconsistent connection state (internal error, "
            "connection and statement objects are neither both null nor both non-null)");
        return nullptr;
    }
    return sqliteDbData;
}

}  // namespace elog

#endif  // ELOG_ENABLE_MYSQL_DB_CONNECTOR