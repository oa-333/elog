#ifndef __ELOG_DB_TARGET_H__
#define __ELOG_DB_TARGET_H__

#include <condition_variable>
#include <mutex>
#include <thread>

#include "elog_db_formatter.h"
#include "elog_target.h"

namespace elog {

/** @def Attempt reconnect every second. */
#define ELOG_DB_RECONNECT_TIMEOUT_MILLIS 1000

/** @brief Abstract parent class for DB log targets. */
class ELOG_API ELogDbTarget : public ELogTarget {
public:
    /** @brief Threading model constants. */
    enum class ThreadModel : uint32_t {
        /**
         * @brief No threading model employed by the db target. The caller is responsible for
         * multi-threaded access to underlying database objects (connection, prepared statement,
         * etc.).
         */
        TM_NONE,

        /** @brief All access to database objects will be serialized with a single lock. */
        TM_LOCK,

        /**
         * @brief Database objects (connection, prepared statement, etc.) will be duplicated on a
         * per-thread basis. No lock is used.
         */
        TM_CONN_PER_THREAD
    };

protected:
    // if maxThreads is zero, then the number configured during elog::initialize() will be used
    // the user is allowed here to override the value specified during elog::initialize()
    ELogDbTarget(const char* dbName, const char* rawInsertStatement,
                 ELogDbFormatter::QueryStyle queryStyle,
                 ThreadModel threadModel = ThreadModel::TM_LOCK, uint32_t maxThreads = 0,
                 uint64_t reconnectTimeoutMillis = ELOG_DB_RECONNECT_TIMEOUT_MILLIS);
    ELogDbTarget(const ELogDbTarget&) = delete;
    ELogDbTarget(ELogDbTarget&&) = delete;
    ELogDbTarget& operator=(const ELogDbTarget&) = delete;

    ~ELogDbTarget() override {}

    /**
     * @brief Notifies the log target that it has turned thread-safe. Derived class may take
     * special measures. The DB log target removes all threading considerations in this case.
     */
    void onThreadSafe() override { m_threadModel = ThreadModel::TM_NONE; }

    /** @brief Order the log target to start (required for threaded targets). */
    bool startLogTarget() override;

    /** @brief Order the log target to stop (required for threaded targets). */
    bool stopLogTarget() override;

    /** @brief Sends a log record to a log target. */
    uint32_t writeLogRecord(const ELogRecord& logRecord) override;

    /** @brief Orders a buffered log target to flush it log messages. */
    bool flushLogTarget() final { return true; }

    /**
     * @brief Retrieves the processed insert statement resulting from the call to @ref
     * parseInsertStatement().
     */
    inline const std::string& getProcessedInsertStatement() const {
        return m_formatter.getProcessedStatement();
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
        m_formatter.fillInsertStatement(logRecord, receptor);
    }

    /** @brief Performs target level initialization. */
    virtual void initDbTarget() {}

    /** @brief Allocates database access object. */
    virtual void* allocDbData() = 0;

    /** @brief Frees database access object. */
    virtual void freeDbData(void* dbData) = 0;

    /** @brief Initializes database access object. */
    virtual bool connectDb(void* dbData) = 0;

    /** @brief Terminates database access object. */
    virtual bool disconnectDb(void* dbData) = 0;

    /** @brief Sends a log record to a log target. */
    virtual bool execInsert(const ELogRecord& logRecord, void* dbData) = 0;

private:
    // identification
    std::string m_dbName;

    // insert statement parsing members
    ELogDbFormatter m_formatter;
    std::string m_rawInsertStatement;
    std::vector<ELogDbFormatter::ParamType> m_paramTypes;

    ThreadModel m_threadModel;
    uint32_t m_maxThreads;
    uint64_t m_reconnectTimeoutMillis;

    // single thread slot
    struct ThreadSlot {
        std::atomic<bool> m_isUsed;
        std::atomic<bool> m_isConnected;
        void* m_dbData;

        ThreadSlot() : m_isUsed(false), m_isConnected(false), m_dbData(nullptr) {}
        ThreadSlot(const ThreadSlot& slot) {
            m_isUsed.store(slot.m_isUsed.load(std::memory_order_relaxed));
            m_isConnected.store(slot.m_isConnected.load(std::memory_order_relaxed));
            m_dbData = slot.m_dbData;
        }
        ThreadSlot(ThreadSlot&&) = delete;

        inline ThreadSlot& operator=(const ThreadSlot& slot) {
            m_isUsed.store(slot.m_isUsed.load(std::memory_order_relaxed),
                           std::memory_order_relaxed);
            m_isConnected.store(slot.m_isConnected.load(std::memory_order_relaxed),
                                std::memory_order_relaxed);
            m_dbData = slot.m_dbData;
            return *this;
        }
    };

    std::vector<ThreadSlot> m_threadSlots;

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

    /** @brief Queries whether that database connection has been restored. */
    inline bool isConnected(uint32_t slotId) {
        return m_threadSlots[slotId].m_isConnected.load(std::memory_order_relaxed);
    }

    /** @brief Sets the database connection as connected. */
    inline void setConnected(uint32_t slotId) {
        m_threadSlots[slotId].m_isConnected.store(true, std::memory_order_relaxed);
    }

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