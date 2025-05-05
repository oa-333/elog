#include "elog_sqlite_db_target.h"

#ifdef ELOG_ENABLE_SQLITE_DB_CONNECTOR

#include <cstring>

#include "elog_system.h"

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
    void receiveStringField(const std::string& field, int justify) {
        int res = sqlite3_bind_text(m_stmt, m_fieldNum++, field.c_str(), field.length(),
                                    SQLITE_TRANSIENT);
        if (m_res == 0) {
            m_res = res;
        }
    }

    /** @brief Receives an integer log record field. */
    void receiveIntField(uint64_t field, int justify) final {
        int res = sqlite3_bind_int64(m_stmt, m_fieldNum++, field);
        if (m_res == 0) {
            m_res = res;
        }
    }

#ifdef ELOG_MSVC
    /** @brief Receives a time log record field. */
    void receiveTimeField(const SYSTEMTIME& sysTime, const char* timeStr, int justify) final {
        int res = sqlite3_bind_text(m_stmt, m_fieldNum++, timeStr, -1, SQLITE_TRANSIENT);
        if (m_res == 0) {
            m_res = res;
        }
    }
#else
    /** @brief Receives a time log record field. */
    void receiveTimeField(const timeval& sysTime, const char* timeStr, int justify) final {
        int res =
            sqlite3_bind_text(m_stmt, m_fieldNum++, timeStr, strlen(timeStr), SQLITE_TRANSIENT);
        if (m_res == 0) {
            m_res = res;
        }
    }
#endif

    /** @brief Receives a log level log record field. */
    void receiveLogLevelField(ELogLevel logLevel, int justify) final {
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
        ELogSystem::reportError("Failed to open sqlite db at path %s: %s", m_filePath.c_str(),
                                sqlite3_errstr(res));
        return false;
    }

    res = sqlite3_prepare_v2(m_connection, processedInsertStmt.c_str(),
                             processedInsertStmt.length(), &m_insertStmt, nullptr);
    if (res != SQLITE_OK) {
        ELogSystem::reportError("Failed to prepare sqlite statement '%s': %s",
                                processedInsertStmt.c_str(), sqlite3_errstr(res));
        return false;
    }
    return true;
}

bool ELogSQLiteDbTarget::stop() {
    if (m_insertStmt != nullptr) {
        int res = sqlite3_finalize(m_insertStmt);
        if (res != SQLITE_OK) {
            ELogSystem::reportError("Failed to destroy sqlite statement: %s", sqlite3_errstr(res));
            return false;
        }
        m_insertStmt = nullptr;
    }

    if (m_connection != nullptr) {
        int res = sqlite3_close_v2(m_connection);
        if (res != SQLITE_OK) {
            ELogSystem::reportError("Failed to close sqlite connection: %s", sqlite3_errstr(res));
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

    // reset statement parameters
    int res = sqlite3_reset(m_insertStmt);
    if (res != SQLITE_OK) {
        ELogSystem::reportError("Failed to reset sqlite statement: %s", sqlite3_errstr(res));
        return;
    }

    // this puts each log record field into the correct place in the prepared statement
    ELogSQLiteDbFieldReceptor sqliteFieldReceptor(m_insertStmt);
    fillInsertStatement(logRecord, &sqliteFieldReceptor);
    res = sqliteFieldReceptor.getRes();
    if (res != SQLITE_OK) {
        ELogSystem::reportError("Failed to bind sqlite statement parameters: %s",
                                sqlite3_errstr(res));
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
    ELogSystem::reportError("Failed to execute sqlite statement parameters: %s",
                            sqlite3_errstr(res));
}

}  // namespace elog

#endif  // ELOG_ENABLE_MYSQL_DB_CONNECTOR