#include "elog_deferred_target.h"

#include <cassert>

#include "elog_system.h"

#define ELOG_FLUSH_REQUEST ((uint16_t)-1)

namespace elog {

bool ELogDeferredTarget::startLogTarget() {
    if (!m_endTarget->start()) {
        return false;
    }
    m_logThread = std::thread(&ELogDeferredTarget::logThread, this);
    return true;
}

bool ELogDeferredTarget::stopLogTarget() {
    stopLogThread();
    return m_endTarget->stop();
}

uint32_t ELogDeferredTarget::writeLogRecord(const ELogRecord& logRecord) {
    m_writeCount.fetch_add(1, std::memory_order_relaxed);
    std::unique_lock<std::mutex> lock(m_lock);
    m_logQueue.emplace_back(logRecord, logRecord.m_logMsg);
    m_cv.notify_one();
    // asynchronous log targets do not report byte count
    return 0;
}

void ELogDeferredTarget::flushLogTarget() {
    // log empty message, which designated a flush request
    // NOTE: there is no waiting for flush to complete
    ELOG_CACHE_ALIGN ELogRecord flushRecord;
    flushRecord.m_logMsg = "";
    flushRecord.m_reserved = ELOG_FLUSH_REQUEST;
    std::unique_lock<std::mutex> lock(m_lock);
    m_logQueue.emplace_back(flushRecord, flushRecord.m_logMsg);
    m_cv.notify_one();
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

        // write to log target outside of lock scope (allow loggers to push messages)
        logQueueMsgs(logQueue, true);
    }

    // log whatever is left (no lock required)
    logQueueMsgs(m_logQueue, false);

    // finally flush
    m_endTarget->flush();
}

bool ELogDeferredTarget::shouldStop() {
    std::unique_lock<std::mutex> lock(m_lock);
    return m_stop;
}

void ELogDeferredTarget::waitQueue(std::unique_lock<std::mutex>& lock) {
    m_cv.wait(lock, [this] { return m_stop || !m_logQueue.empty(); });
}

void ELogDeferredTarget::logQueueMsgs(LogQueue& logQueue, bool disregardFlushRequests) {
    LogQueue::iterator itr = logQueue.begin();
    while (itr != logQueue.end()) {
        ELogRecord& logRecord = itr->first;
        if (logRecord.m_reserved == ELOG_FLUSH_REQUEST) {
            // empty log message signifies flush request (ignored during last time)
            if (!disregardFlushRequests) {
                m_endTarget->flush();
            }
        } else {
            std::string& logMsg = itr->second;
            logRecord.m_logMsg = itr->second.c_str();
            m_endTarget->log(logRecord);
            m_readCount.fetch_add(1, std::memory_order_relaxed);
        }
        ++itr;
    }
    logQueue.clear();
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