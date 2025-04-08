#ifndef __DEFERRED_LOG_TARGET_H__
#define __DEFERRED_LOG_TARGET_H__

#include <condition_variable>
#include <list>
#include <mutex>
#include <thread>

#include "elog_target.h"

namespace elog {

// TODO: consider having lock-free implementation with wakeup/batch timeout
// we call this a pseudo-quantum log target due to its (alleged) ability to not affect the observed
// system, since it allows logging messages with minimal overhead (besides log formatting, which can
// be optimized too, by using a ring of log buffers, which can assist in reducing memcpy() calls -
// this log buffer ring will be implemented by a mult-threaded quantum logger). This of course comes
// with a larger memory footprint.

/**
 * @brief A utility log target for deferring logging to another context. Log formatting through
 * still takes places at the caller's context. For an even shorter deferring latency consider using
 * the @ref ELogQueuedTarget @ref ELogQuantumTarget.
 */
class ELogDeferredTarget : public ELogTarget {
public:
    /**
     * @brief Construct a new ELogDeferredTarget object.
     * @param logTarget The deferred log target.
     */
    ELogDeferredTarget(ELogTarget* logTarget) : m_logTarget(logTarget), m_stop(false) {}
    ~ELogDeferredTarget() override {}

    /** @brief Order the log target to start (required for threaded targets). */
    bool start() final;

    /** @brief Order the log target to stop (required for threaded targets). */
    bool stop() final;

    /** @brief Sends a log record to a log target. */
    void log(const ELogRecord& logRecord) override;

    /**
     * @brief Orders a buffered log target to flush it log messages. Note that flush policy is
     * already managed by the deferred log target. If you wish to enforce flush from outside, then
     * use this call.
     */
    void flush() override;

protected:
    typedef std::list<std::pair<ELogRecord, std::string>> LogQueue;

    ELogTarget* m_logTarget;
    std::thread m_logThread;
    LogQueue m_logQueue;
    std::mutex m_lock;
    std::condition_variable m_cv;
    bool m_stop;

    void logThread();

    bool shouldStop();

    virtual void waitQueue(std::unique_lock<std::mutex>& lock);

    void stopLogThread();
};

}  // namespace elog

#endif  // __DEFERRED_LOG_TARGET_H__