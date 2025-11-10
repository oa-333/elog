#ifndef __ELOG_DB_TARGET_H__
#define __ELOG_DB_TARGET_H__

#include <condition_variable>
#include <mutex>
#include <thread>

#include "elog_atomic.h"
#include "elog_db_formatter.h"
#include "elog_target.h"

// TODO: think about backlog if connection is down...

namespace elog {

/** @brief The default connection pool size. */
#define ELOG_DB_DEFAULT_CONN_POOL_SIZE 4

/** @def Attempt reconnect every second. */
#define ELOG_DB_RECONNECT_TIMEOUT_MILLIS 1000

/** @brief Database threading model constants. */
enum class ELogDbThreadModel : uint32_t {
    /**
     * @brief No threading model employed by the db target. The caller is responsible for
     * multi-threaded access to underlying database objects (connection, prepared statement,
     * etc.).
     */
    TM_NONE,

    /**
     * @brief All access to database objects will be serialized with a single lock and will use
     * a single database connection.
     */
    TM_LOCK,

    /**
     * @brief Database objects (connection, prepared statement, etc.) will be duplicated on a
     * per-thread basis. No lock is used.
     */
    TM_CONN_PER_THREAD,

    /**
     * @brief A fixed-size pool of database connections will be used to communicate with the
     * database. Each log record sending will use any available connection at the moment of
     * sending log data to the database.
     */
    TM_CONN_POOL
};

/** @brief Common database target configuration. */
struct ELOG_API ELogDbConfig {
    /** @brief The database connection string. May contain just host name or IP address. */
    std::string m_connString;

    /** @brief The insert query used to insert log records into the target database. */
    std::string m_insertQuery;

    /** @brief The thread model being used to access the database (e.g. connection pooling). */
    ELogDbThreadModel m_threadModel;

    /**
     * @brief The connection pool size used to access the database. When using thread model
     * other than @ref ELogDbThreadModel::TM_CONN_POOL, this value is ignored, and the maximum
     * number of threads specified during @ref elog::initialize() is used.
     */
    uint32_t m_poolSize;

    /** @brief The reconnect timeout used by the background reconnect task. */
    uint64_t m_reconnectTimeoutMillis;

    ELogDbConfig()
        : m_threadModel(ELogDbThreadModel::TM_LOCK),
          m_poolSize(0),
          m_reconnectTimeoutMillis(ELOG_DB_RECONNECT_TIMEOUT_MILLIS) {}
    ELogDbConfig(const ELogDbConfig&) = default;
    ELogDbConfig(ELogDbConfig&&) = default;
    ELogDbConfig& operator=(const ELogDbConfig&) = default;
    ~ELogDbConfig() {}
};

/** @brief Abstract parent class for DB log targets. */
class ELOG_API ELogDbTarget : public ELogTarget {
public:
protected:
    /**
     * @brief Construct a new ELogDbTarget object
     *
     * @param dbName The database name (for logging purposes only).
     * @param dbConfig Common database access attributes.
     * @param queryStyle The query style used to prepare the insert statement.
     */
    ELogDbTarget(const char* dbName, const ELogDbConfig& dbConfig,
                 ELogDbFormatter::QueryStyle queryStyle);
    ELogDbTarget(const ELogDbTarget&) = delete;
    ELogDbTarget(ELogDbTarget&&) = delete;
    ELogDbTarget& operator=(const ELogDbTarget&) = delete;

    ~ELogDbTarget() override {}

    /**
     * @brief Notifies the log target that it has turned thread-safe. Derived class may take
     * special measures. The DB log target removes all threading considerations in this case.
     */
    void onThreadSafe() override { m_threadModel = ELogDbThreadModel::TM_NONE; }

    /** @brief Order the log target to start (required for threaded targets). */
    bool startLogTarget() override;

    /** @brief Order the log target to stop (required for threaded targets). */
    bool stopLogTarget() override;

    /**
     * @brief Order the log target to write a log record (thread-safe).
     * @param logRecord The log record to write to the log target.
     * @param bytesWritten The number of bytes written to log.
     * @return The operation's result.
     */
    bool writeLogRecord(const ELogRecord& logRecord, uint64_t& bytesWritten) override;

    /** @brief Orders a buffered log target to flush it log messages. */
    bool flushLogTarget() final { return true; }

    /**
     * @brief Retrieves the processed insert statement resulting from the call to @ref
     * parseInsertStatement().
     */
    inline const std::string& getProcessedInsertStatement() const {
        return m_dbFormatter->getProcessedStatement();
    }

    /**
     * @brief Retrieves the parameter type list of the processed insert statement resulting from the
     * call to @ref parseInsertStatement().
     */
    inline const std::vector<ELogDbFormatter::ParamType>& getInsertStatementParamTypes() const {
        return m_paramTypes;
    }

    /**
     * @brief Applies all field selectors to the given log record, so that all prepared statement
     * parameters are filled.
     * @param logRecord The log record to process.
     * @param receptor The receptor that receives log record fields and transfers them to the
     * prepared statement parameters.
     */
    inline void fillInsertStatement(const elog::ELogRecord& logRecord,
                                    elog::ELogFieldReceptor* receptor) {
        m_dbFormatter->fillInsertStatement(logRecord, receptor);
    }

    /** @brief Performs target level initialization. */
    virtual bool initDbTarget() { return true; }

    /** @brief Performs target level termination. */
    virtual void termDbTarget() {}

    /** @brief Allocates database access object. */
    virtual void* allocDbData() = 0;

    /** @brief Frees database access object. */
    virtual void freeDbData(void* dbData) = 0;

    /** @brief Initializes database access object. */
    virtual bool connectDb(void* dbData) = 0;

    /** @brief Terminates database access object. */
    virtual bool disconnectDb(void* dbData) = 0;

    /** @brief Sends a log record to a log target. */
    virtual bool execInsert(const ELogRecord& logRecord, void* dbData, uint64_t& bytesWritten) = 0;

private:
    // identification
    std::string m_dbName;

    // insert statement parsing members
    ELogDbFormatter* m_dbFormatter;
    std::string m_rawInsertStatement;
    ELogDbFormatter::QueryStyle m_queryStyle;
    std::vector<ELogDbFormatter::ParamType> m_paramTypes;

    ELogDbThreadModel m_threadModel;
    uint32_t m_poolSize;
    uint64_t m_reconnectTimeoutMillis;

    // single Connection Data
    class ConnectionData {
    public:
        ConnectionData()
            : m_isUsed(false),
              m_isExecuting(false),
              m_connectState(ConnectState::CS_DISCONNECTED),
              m_dbData(nullptr) {}
        ConnectionData(const ConnectionData& connData)
            : m_isUsed(connData.m_isUsed),
              m_isExecuting(connData.m_isExecuting),
              m_connectState(connData.m_connectState),
              m_dbData(connData.m_dbData) {}
        ConnectionData(ConnectionData&&) = delete;

        ConnectionData& operator=(const ConnectionData& connData) = delete;
        ~ConnectionData() {}

        inline bool isUsed() { return m_isUsed.m_atomicValue.load(std::memory_order_relaxed); }

        inline bool setUsed() {
            bool isUsed = m_isUsed.m_atomicValue.load(std::memory_order_relaxed);
            return !isUsed && m_isUsed.m_atomicValue.compare_exchange_strong(
                                  isUsed, true, std::memory_order_seq_cst);
        }

        inline void setUnused() { m_isUsed.m_atomicValue.store(false, std::memory_order_relaxed); }

        inline bool isExecuting() {
            return m_isExecuting.m_atomicValue.load(std::memory_order_relaxed);
        }

        inline bool setExecuting() {
            bool isExecuting = m_isExecuting.m_atomicValue.load(std::memory_order_relaxed);
            return !isExecuting && m_isExecuting.m_atomicValue.compare_exchange_strong(
                                       isExecuting, true, std::memory_order_seq_cst);
        }

        inline void setNotExecuting() {
            m_isExecuting.m_atomicValue.store(false, std::memory_order_relaxed);
        }

        /** @brief Queries whether that database connection has been restored. */
        inline bool isConnected() {
            return m_connectState.m_atomicValue.load(std::memory_order_relaxed) ==
                   ConnectState::CS_CONNECTED;
        }

        /** @brief Queries whether that database connection is not valid. */
        inline bool isDisconnected() {
            return m_connectState.m_atomicValue.load(std::memory_order_relaxed) ==
                   ConnectState::CS_DISCONNECTED;
        }

        inline bool setConnecting() {
            ConnectState connectState =
                m_connectState.m_atomicValue.load(std::memory_order_relaxed);
            return (connectState == ConnectState::CS_DISCONNECTED) &&
                   m_connectState.m_atomicValue.compare_exchange_strong(
                       connectState, ConnectState::CS_CONNECTING);
        }

        /** @brief Sets the database connection as connected. */
        inline void setConnected() {
            std::unique_lock<std::mutex> lock(m_lock);
            m_connectState.m_atomicValue.store(ConnectState::CS_CONNECTED,
                                               std::memory_order_relaxed);
            m_cv.notify_one();
        }

        /** @brief Sets the database connection as disconnected. */
        inline void setDisconnected() {
            std::unique_lock<std::mutex> lock(m_lock);
            m_connectState.m_atomicValue.store(ConnectState::CS_DISCONNECTED,
                                               std::memory_order_relaxed);
            m_cv.notify_one();
        }

        inline bool waitConnect() {
            std::unique_lock<std::mutex> lock(m_lock);
            m_cv.wait(lock, [this] { return isConnected() || isDisconnected(); });
            return isConnected();
        }

        inline void* getDbData() { return m_dbData; }

        inline void setDbData(void* dbData) { m_dbData = dbData; }

        inline void clearDbData() { m_dbData = nullptr; }

    private:
        ELogAtomic<bool> m_isUsed;
        ELogAtomic<bool> m_isExecuting;  // for connection pool mode
        enum ConnectState : uint32_t { CS_DISCONNECTED, CS_CONNECTING, CS_CONNECTED };
        ELogAtomic<ConnectState> m_connectState;
        std::mutex m_lock;
        std::condition_variable m_cv;
        void* m_dbData;
    };

    std::vector<ConnectionData> m_connectionPool;

    std::thread m_reconnectDbThread;
    std::mutex m_lock;
    std::condition_variable m_cv;
    bool m_shouldStop;
    bool m_shouldWakeUp;

    /**
     * @brief Parses the insert statement loaded from configuration, builds all log record field
     * selectors, and transforms the insert statement into DB acceptable format (i.e. with questions
     * marks as place-holders or dollar sign with parameter ordinal number).
     * @param insertStatement The insert statement to parse.
     * @return true If succeeded, otherwise false.
     */
    bool parseInsertStatement(const std::string& insertStatement);

    uint32_t allocSlot();

    void freeSlot(uint32_t slot);

    bool initConnection(uint32_t& slotId);

    void termConnection(uint32_t slotId);

    bool initConnectionPool();

    bool termConnectionPool();

    /** @brief Queries whether that database connection has been restored. */
    inline bool isConnected(uint32_t slotId) { return m_connectionPool[slotId].isConnected(); }

    inline bool setConnecting(uint32_t slotId) { return m_connectionPool[slotId].setConnecting(); }

    /** @brief Sets the database connection as connected. */
    inline void setConnected(uint32_t slotId) { m_connectionPool[slotId].setConnected(); }

    /** @brief Sets the database connection as disconnected. */
    inline void setDisconnected(uint32_t slotId) { m_connectionPool[slotId].setDisconnected(); }

    /** @brief Helper method for derived classes to reconnect to database. */
    void startReconnect();

    /** @brief Helper method to stop the reconnect thread. */
    void stopReconnect();

    void reconnectTask();

    void wakeUpReconnect();

    bool shouldStop();
};

}  // namespace elog

#endif  // __ELOG_DB_TARGET_H__