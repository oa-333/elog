#include "elog_deferred_target.h"

#include <cassert>

#include "elog_system.h"

namespace elog {

bool ELogDeferredTarget::start() {
    if (!m_logTarget->start()) {
        return false;
    }
    m_logThread = std::thread(&ELogDeferredTarget::logThread, this);
    return true;
}

bool ELogDeferredTarget::stop() {
    stopLogThread();
    return m_logTarget->stop();
}

void ELogDeferredTarget::log(const ELogRecord& logRecord) {
    if (shouldLog(logRecord)) {
        m_writeCount.fetch_add(1, std::memory_order_relaxed);
        std::unique_lock<std::mutex> lock(m_lock);
        m_logQueue.push_back(std::make_pair(logRecord, logRecord.m_logMsg));
        m_cv.notify_one();
    }
}

void ELogDeferredTarget::flush() {
    // log empty message, which designated a flush request
    // NOTE: there is no waiting for flush to complete
    ELogRecord dummy;
    dummy.m_logMsg = "";
    log(dummy);
}

void ELogDeferredTarget::logThread() {
    LogQueue logQueue;
    while (!shouldStop()) {
        {
            // wait for queue event
            std::unique_lock<std::mutex> lock(m_lock);
            waitQueue(lock);
            if (m_stop) {
                break;
            }
            if (m_logQueue.empty()) {
                continue;
            }

            // drain queue (lock still held)
            LogQueue::iterator itr = m_logQueue.begin();
            while (itr != m_logQueue.end()) {
                logQueue.push_back(*itr);
                ++itr;
            }
            m_logQueue.clear();
        }

        // defer log messages to log target out of lock context (out of lock scope)
        LogQueue::iterator itr = logQueue.begin();
        while (itr != logQueue.end()) {
            std::string logMsg = (*itr).second;
            if (logMsg.empty()) {
                // empty log message signifies flush request
                m_logTarget->flush();
            } else {
                ELogRecord& logRecord = (*itr).first;
                logRecord.m_logMsg = (*itr).second.c_str();
                m_logTarget->log(logRecord);
            }
            m_readCount.fetch_add(1, std::memory_order_relaxed);
            ++itr;
        }
        logQueue.clear();
    }

    // log whatever is left (no lock required)
    LogQueue::iterator itr = m_logQueue.begin();
    while (itr != m_logQueue.end()) {
        std::string logMsg = (*itr).second;
        if (!logMsg.empty()) {
            ELogRecord& logRecord = (*itr).first;
            logRecord.m_logMsg = (*itr).second.c_str();
            m_logTarget->log(logRecord);
        }
        m_readCount.fetch_add(1, std::memory_order_relaxed);
        ++itr;
    }

    // finally flush
    m_logTarget->flush();
}

bool ELogDeferredTarget::shouldStop() {
    std::unique_lock<std::mutex> lock(m_lock);
    return m_stop;
}

void ELogDeferredTarget::waitQueue(std::unique_lock<std::mutex>& lock) {
    m_cv.wait(lock, [this] { return m_stop || !m_logQueue.empty(); });
}

void ELogDeferredTarget::stopLogThread() {
    {
        std::unique_lock<std::mutex> lock(m_lock);
        assert(!m_stop);
        m_stop = true;
        m_cv.notify_one();
    }
    m_logThread.join();
}

}  // namespace elog