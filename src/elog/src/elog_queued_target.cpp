#include "elog_queued_target.h"

namespace elog {

ELogQueuedTarget::ELogQueuedTarget(ELogTarget* logTarget, uint32_t batchSize,
                                   uint64_t timeoutMillis)
    : ELogDeferredTarget(logTarget), m_batchSize(batchSize), m_timeoutMillis(timeoutMillis) {}

ELogQueuedTarget::~ELogQueuedTarget() {}

void ELogQueuedTarget::waitQueue(std::unique_lock<std::mutex>& lock) {
    m_cv.wait_for(lock, m_timeoutMillis,
                  [this]() { return m_stop || m_logQueue.size() >= m_batchSize; });
}
}  // namespace elog
