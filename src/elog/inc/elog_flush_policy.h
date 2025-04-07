#ifndef __ELOG_FLUSH_POLICY_H__
#define __ELOG_FLUSH_POLICY_H__

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

#include "elog_target.h"

namespace elog {

/**
 * @brief Flush policy. As some log targets are buffered, a flush policy should be defined to govern
 * the occasions on which the log target should be flushed so that log messages reach their
 * designated destination.
 */
class ELogFlushPolicy {
public:
    virtual ~ELogFlushPolicy() {}
    virtual bool shouldFlush(uint32_t msgSizeBytes) = 0;

protected:
    ELogFlushPolicy() {}
    ELogFlushPolicy(const ELogFlushPolicy&) = delete;
    ELogFlushPolicy(ELogFlushPolicy&&) = delete;
};

/** @class A combined flush policy, for enforcing several flush policies. */
class ELogCombinedFlushPolicy : public ELogFlushPolicy {
public:
    ELogCombinedFlushPolicy() {}
    ELogCombinedFlushPolicy(const ELogCombinedFlushPolicy&) = delete;
    ELogCombinedFlushPolicy(ELogCombinedFlushPolicy&&) = delete;
    ~ELogCombinedFlushPolicy() {}

    inline void addFlushPolicy(ELogFlushPolicy* flushPolicy) {
        m_flushPolicies.push_back(flushPolicy);
    }

protected:
    std::vector<ELogFlushPolicy*> m_flushPolicies;
};

/** @class A combined flush policy, for enforcing all specified flush policies. */
class ELogAndFlushPolicy : public ELogCombinedFlushPolicy {
public:
    ELogAndFlushPolicy() {}
    ELogAndFlushPolicy(const ELogAndFlushPolicy&) = delete;
    ELogAndFlushPolicy(ELogAndFlushPolicy&&) = delete;
    ~ELogAndFlushPolicy() {}

    bool shouldFlush(uint32_t msgSizeBytes) final {
        for (ELogFlushPolicy* flushPolicy : m_flushPolicies) {
            if (!flushPolicy->shouldFlush(msgSizeBytes)) {
                return false;
            }
        }
        return true;
    }
};

/** @class A combined flush policy, for enforcing one of many flush policies. */
class ELogOrFlushPolicy : public ELogCombinedFlushPolicy {
public:
    ELogOrFlushPolicy() {}
    ELogOrFlushPolicy(const ELogOrFlushPolicy&) = delete;
    ELogOrFlushPolicy(ELogOrFlushPolicy&&) = delete;
    ~ELogOrFlushPolicy() {}

    bool shouldFlush(uint32_t msgSizeBytes) final {
        for (ELogFlushPolicy* flushPolicy : m_flushPolicies) {
            if (flushPolicy->shouldFlush(msgSizeBytes)) {
                return true;
            }
        }
        return false;
    }
};

/** @class A immediate flush policy, for enforcing log target flush after every log message.  */
class ELogImmediateFlushPolicy : public ELogFlushPolicy {
public:
    ELogImmediateFlushPolicy() {}
    ELogImmediateFlushPolicy(const ELogImmediateFlushPolicy&) = delete;
    ELogImmediateFlushPolicy(ELogImmediateFlushPolicy&&) = delete;
    ~ELogImmediateFlushPolicy() {}

    bool shouldFlush(uint32_t msgSizeBytes) final { return true; }
};

/**
 * @class A flush policy the enforces log target flush whenever the number of un-flushed log
 * messages exceeds a configured limit.
 */
class ELogCountFlushPolicy : public ELogFlushPolicy {
public:
    ELogCountFlushPolicy(uint64_t logCountLimit)
        : m_logCountLimit(logCountLimit), m_currentLogCount(0) {}
    ELogCountFlushPolicy(const ELogCountFlushPolicy&) = delete;
    ELogCountFlushPolicy(ELogCountFlushPolicy&&) = delete;
    ~ELogCountFlushPolicy() {}

    bool shouldFlush(uint32_t msgSizeBytes) final {
        uint64_t logCount = m_currentLogCount.fetch_add(1, std::memory_order_relaxed);
        return (logCount % m_logCountLimit == 0);
    }

private:
    const uint64_t m_logCountLimit;
    std::atomic<uint64_t> m_currentLogCount;
};

/**
 * @class A flush policy the enforces log target flush whenever the total size of un-flushed log
 * messages exceeds a configured limit.
 */
class ELogSizeFlushPolicy : public ELogFlushPolicy {
public:
    ELogSizeFlushPolicy(uint64_t logSizeLimitBytes)
        : m_logSizeLimitBytes(logSizeLimitBytes), m_currentLogSizeBytes(0) {}
    ELogSizeFlushPolicy(const ELogSizeFlushPolicy&) = delete;
    ELogSizeFlushPolicy(ELogSizeFlushPolicy&&) = delete;
    ~ELogSizeFlushPolicy() {}

    bool shouldFlush(uint32_t msgSizeBytes) final {
        uint64_t prevSizeBytes =
            m_currentLogSizeBytes.fetch_add(msgSizeBytes, std::memory_order_relaxed);
        uint64_t currSizeBytes = prevSizeBytes + msgSizeBytes;
        return (currSizeBytes % m_logSizeLimitBytes) > (prevSizeBytes & m_logSizeLimitBytes);
    }

private:
    const uint64_t m_logSizeLimitBytes;
    std::atomic<uint64_t> m_currentLogSizeBytes;
};

/**
 * @class A flush policy the enforces log target flush whenever the time that passed since recent
 * log message flush exceeds a configured time limit. This is an active policy, and it should be
 * combined with a log target, such that when flush time arrives, the log target will get flushed.
 */
class ELogTimedFlushPolicy : public ELogFlushPolicy {
public:
    ELogTimedFlushPolicy(uint64_t logTimeLimitMillis, ELogTarget* logTarget)
        : m_prevFlushTime(getTimestamp()),
          m_logTimeLimitMillis(logTimeLimitMillis),
          m_logTarget(logTarget),
          m_stopTimer(false) {
        m_timerThread = std::thread(onTimer, this);
    }
    ELogTimedFlushPolicy(const ELogTimedFlushPolicy&) = delete;
    ELogTimedFlushPolicy(ELogTimedFlushPolicy&&) = delete;
    ~ELogTimedFlushPolicy() {
        m_stopTimer = true;
        m_timerThread.join();
    }

    bool shouldFlush(uint32_t msgSizeBytes) final {
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

private:
    typedef std::chrono::time_point<std::chrono::steady_clock> Timestamp;
    typedef std::chrono::milliseconds Millis;

    inline Timestamp getTimestamp() const { return std::chrono::steady_clock::now(); }

    inline std::chrono::milliseconds getTimeDiff(Timestamp later, Timestamp earlier) const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(later - earlier);
    }

    const Millis m_logTimeLimitMillis;
    ELogTarget* m_logTarget;
    std::atomic<Timestamp> m_prevFlushTime;
    bool m_stopTimer;
    std::thread m_timerThread;

    void onTimer() {
        while (!m_stopTimer) {
            std::this_thread::sleep_for(std::chrono::milliseconds(m_logTimeLimitMillis));
            // we participate with the rest as a phantom logger, to avoid duplicate flushes
            if (shouldFlush(0)) {
                m_logTarget->flush();
            }
        }
    }
};

}  // namespace elog

#endif  // __ELOG_FLUSH_POLICY_H__