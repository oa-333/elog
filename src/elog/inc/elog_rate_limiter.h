#ifndef __ELOG_RATE_LIMITER_H__
#define __ELOG_RATE_LIMITER_H__

#include <atomic>
#include <chrono>

#include "elog_filter.h"

// NOTE: the simplest implementation is token bucket, because it just requires a counter to be reset
// periodically, yet this implementation is too simplistic, and not so smooth on window edges.
// instead the sliding window counter algorithm was chosen, so that behavior on edges is smooth,
// memory footprint is low, and implementation is not complex. The only downside is that it is not
// strictly accurate, but we are ok with that.
// the implementation relies on incoming log messages, instead of independent timer to count each
// passing second.

namespace elog {

/** @brief Log rate limiter. */
class ELogRateLimiter : public ELogFilter {
public:
    ELogRateLimiter(uint64_t maxMsgPerSecond)
        : m_maxMsgPerSecond(maxMsgPerSecond),
          m_currSecond(0),
          m_currSecondCount(0),
          m_prevSecondCount(0) {}
    ~ELogRateLimiter() final {}

    /**
     * @brief Filters a log record.
     * @param logRecord The log record to filter.
     * @return true If the log record is to be logged.
     * @return false If the log record is to be discarded.
     */
    bool filterLogRecord(const ELogRecord& logRecord) final;

protected:
    uint64_t m_maxMsgPerSecond;
    std::atomic<uint64_t> m_currSecond;
    std::atomic<uint64_t> m_currSecondCount;
    std::atomic<uint64_t> m_prevSecondCount;

    // milliseconds precision is enough
    typedef std::chrono::milliseconds tstamp_t;

    tstamp_t getTstamp();
};

}  // namespace elog

#endif  // __ELOG_RATE_LIMITER_H__