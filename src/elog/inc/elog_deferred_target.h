#ifndef __ELOG_DEFERRED_LOG_TARGET_H__
#define __ELOG_DEFERRED_LOG_TARGET_H__

#include <condition_variable>
#include <list>
#include <mutex>
#include <thread>

#include "elog_def.h"
#include "elog_target.h"

namespace elog {

/**
 * @brief A utility log target for deferring logging to another context. Log formatting through
 * still takes places at the caller's context. For an even shorter deferring latency consider using
 * the @ref ELogQueuedTarget, or the @ref ELogQuantumTarget.
 */
class ELOG_API ELogDeferredTarget : public ELogTarget {
public:
    /**
     * @brief Construct a new ELogDeferredTarget object.
     * @param logTarget The deferred log target.
     */
    ELogDeferredTarget(ELogTarget* logTarget)
        : ELogTarget("deferred"),
          m_logTarget(logTarget),
          m_stop(false),
          m_writeCount(0),
          m_readCount(0) {}
    ~ELogDeferredTarget() override {}

    /** @brief Sends a log record to a log target. */
    void log(const ELogRecord& logRecord) override;

    /**
     * @brief Orders a buffered log target to flush it log messages. Note that flush policy is
     * already managed by the deferred log target. If you wish to enforce flush from outside, then
     * use this call.
     */
    void flush() override;

    /** @brief As log target may be chained as in a list, this retrieves the final log target. */
    ELogTarget* getEndLogTarget() override { return m_logTarget; }

    /** @brief Queries whether the log target has written all pending messages. */
    bool isCaughtUp(uint64_t& writeCount, uint64_t& readCount) final {
        writeCount = m_writeCount.load(std::memory_order_relaxed);
        readCount = m_readCount.load(std::memory_order_relaxed);
        return writeCount == readCount;
    }

protected:
    typedef std::list<std::pair<ELogRecord, std::string>> LogQueue;

    ELogTarget* m_logTarget;
    std::thread m_logThread;
    LogQueue m_logQueue;
    std::mutex m_lock;
    std::condition_variable m_cv;
    bool m_stop;
    std::atomic<uint64_t> m_writeCount;
    std::atomic<uint64_t> m_readCount;

    /** @brief Order the log target to start (required for threaded targets). */
    bool startLogTarget() final;

    /** @brief Order the log target to stop (required for threaded targets). */
    bool stopLogTarget() final;

    void logThread();

    bool shouldStop();

    virtual void waitQueue(std::unique_lock<std::mutex>& lock);

    void stopLogThread();
};

}  // namespace elog

#endif  // __ELOG_DEFERRED_LOG_TARGET_H__