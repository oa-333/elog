#include "elog_flush_policy.h"

namespace elog {

bool ELogAndFlushPolicy::shouldFlush(uint32_t msgSizeBytes) {
    for (ELogFlushPolicy* flushPolicy : m_flushPolicies) {
        if (!flushPolicy->shouldFlush(msgSizeBytes)) {
            return false;
        }
    }
    return true;
}

bool ELogOrFlushPolicy::shouldFlush(uint32_t msgSizeBytes) {
    for (ELogFlushPolicy* flushPolicy : m_flushPolicies) {
        if (flushPolicy->shouldFlush(msgSizeBytes)) {
            return true;
        }
    }
    return false;
}

bool ELogImmediateFlushPolicy::shouldFlush(uint32_t msgSizeBytes) { return true; }

bool ELogNeverFlushPolicy::shouldFlush(uint32_t msgSizeBytes) { return false; }

bool ELogCountFlushPolicy::shouldFlush(uint32_t msgSizeBytes) {
    uint64_t logCount = m_currentLogCount.fetch_add(1, std::memory_order_relaxed);
    return (logCount % m_logCountLimit == 0);
}

bool ELogSizeFlushPolicy::shouldFlush(uint32_t msgSizeBytes) {
    uint64_t prevSizeBytes =
        m_currentLogSizeBytes.fetch_add(msgSizeBytes, std::memory_order_relaxed);
    uint64_t currSizeBytes = prevSizeBytes + msgSizeBytes;
    return (currSizeBytes / m_logSizeLimitBytes) > (prevSizeBytes / m_logSizeLimitBytes);
}

ELogTimedFlushPolicy::ELogTimedFlushPolicy(uint64_t logTimeLimitMillis, ELogTarget* logTarget)
    : m_prevFlushTime(getTimestamp()),
      m_logTimeLimitMillis(logTimeLimitMillis),
      m_logTarget(logTarget),
      m_stopTimer(false) {}

ELogTimedFlushPolicy::~ELogTimedFlushPolicy() {}

bool ELogTimedFlushPolicy::start() {
    m_timerThread = std::thread(&ELogTimedFlushPolicy::onTimer, this);
    return true;
}

bool ELogTimedFlushPolicy::stop() {
    // raise stop flag and wakeup timer thread
    {
        std::unique_lock<std::mutex> lock(m_lock);
        m_stopTimer = true;
        m_cv.notify_one();
    }

    // wait for timer thread to finish
    m_timerThread.join();
    return true;
}

bool ELogTimedFlushPolicy::shouldFlush(uint32_t msgSizeBytes) {
    // start flush thread on demand

    // get timestamp
    Timestamp now = std::chrono::steady_clock::now();

    // compare with previous flush time
    Timestamp prev = m_prevFlushTime.load(std::memory_order_relaxed);
    if (getTimeDiff(now, prev) > m_logTimeLimitMillis) {
        // the one that sets the new flush time will notify caller to flush
        if (m_prevFlushTime.compare_exchange_strong(prev, now, std::memory_order_seq_cst)) {
            return true;
        }
    }
    return false;
}

void ELogTimedFlushPolicy::onTimer() {
    while (!shouldStop()) {
        // wait for timeout or for stop flag to be raised
        {
            std::unique_lock<std::mutex> lock(m_lock);
            m_cv.wait_for(lock, std::chrono::milliseconds(m_logTimeLimitMillis),
                          [this] { return m_stopTimer; });
            if (m_stopTimer) {
                break;
            }
        }

        // we participate with the rest of the concurrent loggers as a phantom logger, so that we
        // avoid duplicate flushes (others call shouldFlush() with some payload size)
        if (shouldFlush(0)) {
            m_logTarget->flush();
        }
    }
}

bool ELogTimedFlushPolicy::shouldStop() {
    std::unique_lock<std::mutex> lock(m_lock);
    return m_stopTimer;
}

}  // namespace elog
