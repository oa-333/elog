#include "db/elog_pgsql_db_target.h"

#ifdef ELOG_ENABLE_PGSQL_DB_CONNECTOR

#include <cstring>
#include <sstream>

#include "elog_report.h"

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogPGSQLDbTarget)

ELOG_IMPLEMENT_LOG_TARGET(ELogPGSQLDbTarget)

class ELogPGSQLDbFieldReceptor : public ELogFieldReceptor {
public:
    ELogPGSQLDbFieldReceptor() {}
    ELogPGSQLDbFieldReceptor(const ELogPGSQLDbFieldReceptor&) = delete;
    ELogPGSQLDbFieldReceptor(ELogPGSQLDbFieldReceptor&&) = delete;
    ELogPGSQLDbFieldReceptor& operator=(const ELogPGSQLDbFieldReceptor&) = delete;
    ~ELogPGSQLDbFieldReceptor() final {}

    /** @brief Receives a string log record field. */
    void receiveStringField(uint32_t typeId, const char* field, const ELogFieldSpec& fieldSpec,
                            size_t length) final {
        m_stringCache.push_back(field);
    }

    /** @brief Receives an integer log record field. */
    void receiveIntField(uint32_t typeId, uint64_t field, const ELogFieldSpec& fieldSpec) final {
        m_stringCache.push_back(std::to_string(field));
    }

    /** @brief Receives a time log record field. */
    void receiveTimeField(uint32_t typeId, const ELogTime& logTime, const char* timeStr,
                          const ELogFieldSpec& fieldSpec, size_t length) final {
        m_stringCache.push_back(timeStr);
    }

    /** @brief Receives a log level log record field. */
    void receiveLogLevelField(uint32_t typeId, ELogLevel logLevel,
                              const ELogFieldSpec& fieldSpec) final {
        const char* logLevelStr = elogLevelToStr(logLevel);
        m_stringCache.push_back(logLevelStr);
    }

    void prepareParams() {
        for (const std::string& str : m_stringCache) {
            m_paramValues.push_back(str.c_str());
            m_paramLengths.push_back((int)str.length());
        }
    }

    inline const char* const* getParamValues() const { return &m_paramValues[0]; }
    inline const int* getParamLengths() const { return &m_paramLengths[0]; }

private:
    std::vector<std::string> m_stringCache;
    std::vector<const char*> m_paramValues;
    std::vector<int> m_paramLengths;
};

bool ELogPGSQLDbTarget::initDbTarget() {
    m_stmtName = "elog_pgsql_insert_stmt";
    m_paramFormats.resize(getInsertStatementParamTypes().size(), 0);
    return true;
}

bool ELogPGSQLDbTarget::connectDb(void* dbData) {
    PGSQLDbData* pgsqlDbData = validateConnectionState(dbData, false);
    if (pgsqlDbData == nullptr) {
        return false;
    }

    // connect to database
    pgsqlDbData->m_conn = PQconnectdb(m_connString.c_str());
    if (pgsqlDbData->m_conn == nullptr) {
        ELOG_REPORT_ERROR("Failed to open PostgreSQL db connection with connection string: %s",
                          m_connString.c_str());
        return false;
    }
    ELOG_REPORT_TRACE("Connected to PG");

    // check connection state
    if (PQstatus(pgsqlDbData->m_conn) != CONNECTION_OK) {
        ELOG_REPORT_ERROR("Failed to open PostgreSQL db connection with connection string '%s': %s",
                          m_connString.c_str(), PQerrorMessage(pgsqlDbData->m_conn));
        PQfinish(pgsqlDbData->m_conn);
        pgsqlDbData->m_conn = nullptr;
        return false;
    }
    ELOG_REPORT_TRACE("PG connection status is OK");

    // prepare statement
    // NOTE: according to libpq documentation, PGresult can return null, so we must be cautious
    PGresult* res =
        PQprepare(pgsqlDbData->m_conn, m_stmtName.c_str(), getProcessedInsertStatement().c_str(),
                  (int)getInsertStatementParamTypes().size(), nullptr);
    if (res == nullptr || PQresultStatus(res) != PGRES_COMMAND_OK) {
        ExecStatusType status = res ? PQresultStatus(res) : PGRES_FATAL_ERROR;
        const char* errStr = res ? (const char*)PQresultErrorMessage(res) : "N/A";
        const char* statusStr = res ? (const char*)PQresStatus(status) : "N/A";
        ELOG_REPORT_ERROR("Failed to prepare PostgreSQL statement '%s': %s (status: %s)",
                          getProcessedInsertStatement().c_str(), errStr, statusStr);
        PQclear(res);
        PQfinish(pgsqlDbData->m_conn);
        pgsqlDbData->m_conn = nullptr;
        return false;
    }
    PQclear(res);
    ELOG_REPORT_TRACE("PG connection and prepared statement are ready");
    return true;
}

bool ELogPGSQLDbTarget::disconnectDb(void* dbData) {
    PGSQLDbData* pgsqlDbData = validateConnectionState(dbData, true);
    if (pgsqlDbData == nullptr) {
        return false;
    }

#ifdef ELOG_MINGW
    PGresult* res = PQclosePrepared(pgsqlDbData->m_conn, m_stmtName.c_str());
    if (res == nullptr || PQresultStatus(res) != PGRES_COMMAND_OK) {
        ExecStatusType status = res ? PQresultStatus(res) : PGRES_FATAL_ERROR;
        const char* errStr = res ? (const char*)PQresultErrorMessage(res) : "N/A";
        const char* statusStr = res ? (const char*)PQresStatus(status) : "N/A";
        ELOG_REPORT_ERROR("Failed to close prepared PostgreSQL statement '%s': %s (status: %s)",
                          m_stmtName.c_str(), errStr, statusStr);
        PQclear(res);
        return false;
    }
    PQclear(res);
#endif

    PQfinish(pgsqlDbData->m_conn);
    pgsqlDbData->m_conn = nullptr;
    return true;
}

bool ELogPGSQLDbTarget::execInsert(const ELogRecord& logRecord, void* dbData) {
    PGSQLDbData* pgsqlDbData = validateConnectionState(dbData, true);
    if (pgsqlDbData == nullptr) {
        return false;
    }

    // this puts each log record field into the correct place in the prepared statement parameters
    ELogPGSQLDbFieldReceptor pgsqlFieldReceptor;
    fillInsertStatement(logRecord, &pgsqlFieldReceptor);
    pgsqlFieldReceptor.prepareParams();

    // execute prepared statement
    PGresult* res = PQexecPrepared(pgsqlDbData->m_conn, m_stmtName.c_str(),
                                   (int)getInsertStatementParamTypes().size(),
                                   pgsqlFieldReceptor.getParamValues(),
                                   pgsqlFieldReceptor.getParamLengths(), &m_paramFormats[0], 0);
    if (res == nullptr || PQresultStatus(res) != PGRES_COMMAND_OK) {
        ExecStatusType status = res ? PQresultStatus(res) : PGRES_FATAL_ERROR;
        const char* errStr = res ? (const char*)PQresultErrorMessage(res) : "N/A";
        const char* statusStr = res ? (const char*)PQresStatus(status) : "N/A";
        std::string logMsg;
        formatLogMsg(logRecord, logMsg);
        ELOG_REPORT_ERROR(
            "Failed to execute prepared PostgreSQL statement: %s (status: %s, log msg: %s)", errStr,
            statusStr, logMsg.c_str());
        PQclear(res);
        // NOTE: returning false here will cause the DB connection to be dropped and trigger
        // reconnection, so unless we are sure that the database connection is dead we should avoid
        // returning false here
        ConnStatusType connStatus = PQstatus(pgsqlDbData->m_conn);
        if (connStatus == CONNECTION_BAD) {
            return false;
        }
        return true;
    }
    PQclear(res);
    return true;
}

void ELogPGSQLDbTarget::formatConnString(const std::string& host, uint32_t port,
                                         const std::string& db, const std::string& user,
                                         const std::string& passwd) {
    std::stringstream s;
    s << "postgresql://" << user << ":" << passwd << "@" << host << ":" << port << "/" << db;
    m_connString = s.str();
}

ELogPGSQLDbTarget::PGSQLDbData* ELogPGSQLDbTarget::validateConnectionState(void* dbData,
                                                                           bool shouldBeConnected) {
    if (dbData == nullptr) {
        ELOG_REPORT_ERROR(
            "Cannot connect to PostgreSQL database, invalid connection state (internal error, "
            "database object is null)");
        return nullptr;
    }
    PGSQLDbData* pgsqlDbData = (PGSQLDbData*)dbData;
    if (shouldBeConnected && pgsqlDbData->m_conn == nullptr) {
        ELOG_REPORT_ERROR(
            "Cannot connect to PostgreSQL database, invalid connection state (internal error, "
            "PG connection is null)");
        return nullptr;
    } else if (!shouldBeConnected && pgsqlDbData->m_conn != nullptr) {
        ELOG_REPORT_ERROR(
            "Cannot connect to PostgreSQL database, invalid connection state (internal error, "
            "PG connection is NOT null as expected)");
        return nullptr;
    }
    return pgsqlDbData;
}

}  // namespace elog

#endif  // ELOG_ENABLE_PGSQL_DB_CONNECTOR