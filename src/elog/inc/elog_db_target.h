#ifndef __ELOG_DB_TARGET_H__
#define __ELOG_DB_TARGET_H__

#include <condition_variable>
#include <mutex>
#include <thread>

#include "elog_db_formatter.h"
#include "elog_target.h"

namespace elog {

/** @brief Abstract parent class for DB log targets. */
class ELogDbTarget : public ELogTarget {
public:
    /** @brief Orders a buffered log target to flush it log messages. */
    void flush() final {}

protected:
    ELogDbTarget(ELogDbFormatter::QueryStyle queryStyle)
        : m_formatter(queryStyle),
          m_isConnected(false),
          m_isReconnecting(false),
          m_shouldStop(false) {}
    ~ELogDbTarget() override {}

    /**
     * @brief Parses the insert statement loaded from configuration, builds all log record field
     * selectors, and transforms the insert statement into DB acceptable format (i.e. with questions
     * marks as place-holders or dollar sign with parameter ordinal number).
     * @param insertStatement The insert statement to parse.
     * @return true If succeeded, otherwise false.
     */
    bool parseInsertStatement(const std::string& insertStatement);

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
    inline void getInsertStatementParamTypes(
        std::vector<ELogDbFormatter::ParamType>& paramTypes) const {
        return m_formatter.getParamTypes(paramTypes);
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

    /** @brief Helper method for derived classes to reconnect to database. */
    void startReconnect(uint32_t reconnectTimeoutMillis = 1000);

    /** @brief Helper method to stop the reconnect thread. */
    void stopReconnect();

    /** @brief Queries whether that database connection has been restored. */
    inline bool isConnected() {
        bool res = m_isConnected.load(std::memory_order_relaxed);
        if (res && m_reconnectDbThread.joinable()) {
            m_reconnectDbThread.join();
        }
        return res;
    }

    /** @brief Sets the database connection as connected. */
    inline void setConnected() { m_isConnected.store(true, std::memory_order_relaxed); }

private:
    ELogDbFormatter m_formatter;
    std::string m_processedInsertQuery;

    std::thread m_reconnectDbThread;
    std::atomic<bool> m_isConnected;
    std::atomic<bool> m_isReconnecting;
    std::mutex m_lock;
    std::condition_variable m_cv;
    bool m_shouldStop;

    void reconnectTask(uint32_t reconnectTimeoutMillis);

    bool shouldStop();
};

}  // namespace elog

#endif  // __ELOG_DB_TARGET_H__