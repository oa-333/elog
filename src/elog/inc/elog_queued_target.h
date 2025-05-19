#ifndef __ELOG_QUEUED_TARGET_H__
#define __ELOG_QUEUED_TARGET_H__

#include <condition_variable>
#include <list>
#include <thread>

#include "elog_deferred_target.h"

namespace elog {

/**
 * @brief A log target which queues log request to be executed on another context. This log target
 * differs from the @ref ELogDeferredTarget in the way it operates. Although both use a queue and a
 * thread to execute messages on another context, the @ref ELogQueuedTarget allows configuring a
 * batch size before notifying the log thread, or a timeout triggers logging in case the configured
 * batch size is not reached within the given timeout. This approach might utilize system resources
 * a bit better, but will result in some latency until log messages are sent to the target. In case
 * of system crash, this might lead to some loss of log messages. The solution for this is to write
 * log messages directly to a shared memory segment, where another process can still examine those
 * messages. This can be done by deriving from ELogTarget and implementing this logic. (Check out
 * the @ref ELogSharedMemTarget for a sample implementation).
 */
class ELOG_API ELogQueuedTarget : public ELogDeferredTarget {
public:
    /**
     * @brief Construct a new ELogQueuedTarget object.
     * @param logTarget The deferred log target.
     */
    ELogQueuedTarget(ELogTarget* logTarget, uint32_t batchSize, uint32_t timeoutMillis);
    ~ELogQueuedTarget() final;

protected:
    void waitQueue(std::unique_lock<std::mutex>& lock) final;

private:
    typedef std::chrono::time_point<std::chrono::steady_clock> Timestamp;
    typedef std::chrono::milliseconds Millis;

    uint32_t m_batchSize;
    Millis m_timeoutMillis;
};

}  // namespace elog

#endif  // __ELOG_QUEUED_TARGET_H__