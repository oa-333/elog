#ifndef __ELOG_DEFERRED_LOG_TARGET_H__
#define __ELOG_DEFERRED_LOG_TARGET_H__

#include <condition_variable>
#include <list>
#include <mutex>
#include <thread>

#include "elog_async_target.h"

namespace elog {

/**
 * @brief A utility log target for deferring logging to another context. Log formatting through
 * still takes places at the caller's context. For an even shorter deferring latency consider using
 * the @ref ELogQueuedTarget, or the @ref ELogQuantumTarget.
 */
class ELOG_API ELogDeferredTarget : public ELogAsyncTarget {
public:
    /**
     * @brief Construct a new ELogDeferredTarget object.
     * @param logTarget The deferred log target.
     */
    ELogDeferredTarget(ELogTarget* logTarget)
        : ELogAsyncTarget(logTarget), m_stop(false), m_writeCount(0), m_readCount(0) {}
    ELogDeferredTarget(const ELogDeferredTarget&) = delete;
    ELogDeferredTarget(ELogDeferredTarget&&) = delete;
    ELogDeferredTarget& operator=(const ELogDeferredTarget&) = delete;

    ELOG_DECLARE_LOG_TARGET_OVERRIDE(ELogDeferredTarget)

protected:
    typedef std::list<std::pair<ELogRecord, std::string>> LogQueue;

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

    /** @brief Sends a log record to a log target. */
    uint32_t writeLogRecord(const ELogRecord& logRecord) override;

    /** @brief Order the log target to flush. */
    bool flushLogTarget() override;

    void logThread();

    bool shouldStop();

    virtual void waitQueue(std::unique_lock<std::mutex>& lock);

    void logQueueMsgs(LogQueue& logQueue, bool disregardFlushRequests);

    void stopLogThread();
};

}  // namespace elog

#endif  // __ELOG_DEFERRED_LOG_TARGET_H__