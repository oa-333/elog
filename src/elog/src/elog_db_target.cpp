#include "elog_db_target.h"

#include "elog_system.h"

namespace elog {

bool ELogDbTarget::parseInsertStatement(const std::string& insertStatement) {
    if (!m_formatter.initialize(insertStatement.c_str())) {
        ELogSystem::reportError("Failed to parse insert statement: %s", insertStatement.c_str());
        return false;
    }
    return true;
}

void ELogDbTarget::startReconnect(uint32_t reconnectTimeoutMillis /* = 1000 */) {
    // set state as not connected
    bool isConnected = m_isConnected.load(std::memory_order_relaxed);
    if (isConnected) {
        if (m_isConnected.compare_exchange_strong(isConnected, false, std::memory_order_seq_cst)) {
            // cleanup previous connection state
            // NOTE: this must be called here, since stop() normally calls stopReconnect()
            // at this moment it will have no effect, but if called after reconnect thread is
            // launched, then it will cause the thread to stop
            stop();

            // launch reconnect thread (only one logging thread will succeed, and only once)
            m_reconnectDbThread =
                std::thread(&ELogDbTarget::reconnectTask, this, reconnectTimeoutMillis);
            m_isReconnecting.store(true, std::memory_order_relaxed);
        }
    }
}

void ELogDbTarget::stopReconnect() {
    // only one thread will succeed, and only once
    bool isReconnecting = m_isReconnecting.load(std::memory_order_relaxed);
    if (isReconnecting) {
        if (m_isReconnecting.compare_exchange_strong(isReconnecting, false,
                                                     std::memory_order_seq_cst)) {
            // notify stop and then join
            {
                std::unique_lock<std::mutex> lock(m_lock);
                m_shouldStop = true;
                m_cv.notify_one();
            }
            m_reconnectDbThread.join();
        }
    }
}

void ELogDbTarget::reconnectTask(uint32_t reconnectTimeoutMillis) {
    // now start reconnect attempt until success or ordered to stop
    while (!shouldStop()) {
        if (start()) {
            // derived class should have already set the connection flag to true, but for
            // safety...
            m_isConnected.store(true, std::memory_order_relaxed);
            break;
        }

        // otherwise wait (interruptible)
        std::unique_lock<std::mutex> lock(m_lock);
        m_cv.wait_for(lock, std::chrono::milliseconds(reconnectTimeoutMillis),
                      [this]() { return m_shouldStop; });
    }
}

bool ELogDbTarget::shouldStop() {
    std::unique_lock<std::mutex> lock(m_lock);
    return m_shouldStop;
}

}  // namespace elog
