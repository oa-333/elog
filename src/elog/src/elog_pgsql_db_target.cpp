#include "elog_pgsql_db_target.h"

#ifdef ELOG_ENABLE_PGSQL_DB_CONNECTOR

#include <cstring>

#include "elog_system.h"

namespace elog {

class ELogPGSQLDbFieldReceptor : public ELogFieldReceptor {
public:
    ELogPGSQLDbFieldReceptor() {}
    ~ELogPGSQLDbFieldReceptor() final {}

    /** @brief Receives a string log record field. */
    void receiveStringField(const std::string& field, int justify) {
        m_stringCache.push_back(field);
    }

    /** @brief Receives an integer log record field. */
    void receiveIntField(uint64_t field, int justify) final {
        m_stringCache.push_back(std::to_string(field));
    }

#ifdef ELOG_MSVC
    /** @brief Receives a time log record field. */
    void receiveTimeField(const SYSTEMTIME& sysTime, const char* timeStr, int justify) final {
        m_stringCache.push_back(timeStr);
    }
#else
    /** @brief Receives a time log record field. */
    void receiveTimeField(const timeval& sysTime, const char* timeStr, int justify) final {
        m_stringCache.push_back(timeStr);
    }
#endif

    /** @brief Receives a log level log record field. */
    void receiveLogLevelField(ELogLevel logLevel, int justify) final {
        const char* logLevelStr = elogLevelToStr(logLevel);
        m_stringCache.push_back(logLevelStr);
    }

    void prepareParams() {
        for (const std::string& str : m_stringCache) {
            m_paramValues.push_back(str.c_str());
            m_paramLengths.push_back(str.length());
        }
    }

    inline const char* const* getParamValues() const { return &m_paramValues[0]; }
    inline const int* getParamLengths() const { return &m_paramLengths[0]; }

private:
    std::vector<std::string> m_stringCache;
    std::vector<const char*> m_paramValues;
    std::vector<int> m_paramLengths;
};

bool ELogPGSQLDbTarget::start() {
    // parse the statement with log record field selector tokens
    // this builds a processed statement text with questions marks instead of log record field
    // references, and also prepares the field selector array
    if (!parseInsertStatement(m_insertStmtText)) {
        return false;
    }
    std::string processedInsertStmt = getProcessedInsertStatement();

    m_connection = PQconnectdb(m_connString.c_str());
    if (m_connection == nullptr) {
        ELogSystem::reportError(
            "Failed to open PostgreSQL db connection with connection string: %s",
            m_connString.c_str());
        return false;
    }
    if (PQstatus(m_connection) != CONNECTION_OK) {
        ELogSystem::reportError(
            "Failed to open PostgreSQL db connection with connection string%s: %s",
            m_connString.c_str(), PQerrorMessage(m_connection));
        PQfinish(m_connection);
        m_connection = nullptr;
        return false;
    }

    // get PG parameter type array
    getInsertStatementParamTypes(m_paramTypes);
    // convertToPgParamTypes();

    // prepare statement
    m_stmtName = "elog_pgsql_insert_stmt";
    PGresult* res = PQprepare(m_connection, m_stmtName.c_str(), processedInsertStmt.c_str(),
                              m_paramTypes.size(), nullptr);
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        ELogSystem::reportError("Failed to prepare PostgreSQL statement '%s': %s",
                                processedInsertStmt.c_str(), PQresultErrorMessage(res));
        m_stmtName.clear();
        PQclear(res);
        PQfinish(m_connection);
        m_connection = nullptr;
        return false;
    }
    PQclear(res);

    // notify parent class about connection state
    setConnected();
    return true;
}

bool ELogPGSQLDbTarget::stop() {
    // stop any reconnect background task
    stopReconnect();

    if (m_connection != nullptr) {
        if (!m_stmtName.empty()) {
#ifdef ELOG_MINGW
            PGresult* res = PQclosePrepared(m_connection, m_stmtName.c_str());
            if (PQresultStatus(res) != PGRES_COMMAND_OK) {
                ELogSystem::reportError("Failed to close prepared PostgreSQL statement: %s",
                                        PQresultErrorMessage(res));
                PQclear(res);
                return false;
            }
            PQclear(res);
#endif
        }
        PQfinish(m_connection);
        m_connection = nullptr;
    }
    return true;
}

void ELogPGSQLDbTarget::log(const ELogRecord& logRecord) {
    if (!shouldLog(logRecord)) {
        return;
    }

    // check if connected to database, otherwise discard log record
    if (!isConnected()) {
        return;
    }

    // this puts each log record field into the correct place in the prepared statement
    ELogPGSQLDbFieldReceptor pgsqlFieldReceptor;
    fillInsertStatement(logRecord, &pgsqlFieldReceptor);
    pgsqlFieldReceptor.prepareParams();

    // execute statement, retry if busy, discard all returned data (there shouldn't be any, though)
    std::vector<int> paramFormats(m_paramTypes.size(), 0);
    PGresult* res = PQexecPrepared(m_connection, m_stmtName.c_str(), m_paramTypes.size(),
                                   pgsqlFieldReceptor.getParamValues(),
                                   pgsqlFieldReceptor.getParamLengths(), &paramFormats[0], 0);
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        ELogSystem::reportError("Failed to execute prepared PostgreSQL statement: %s",
                                PQresultErrorMessage(res));
        PQclear(res);

        // failure to send a record, so order parent class to start reconnect background task
        startReconnect();
        return;
    }
    PQclear(res);
}

void ELogPGSQLDbTarget::formatConnString(const std::string& host, uint32_t port,
                                         const std::string& db, const std::string& user,
                                         const std::string& passwd) {
    std::stringstream s;
    s << "postgresql://postgres@" << host << "?port=" << port << "&dbname=" << db
      << "&user=" << user << "&password=" << passwd;
    m_connString = s.str();
}

}  // namespace elog

#endif  // ELOG_ENABLE_MYSQL_DB_CONNECTOR