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
class ELOG_API ELogRateLimiter final : public ELogCmpFilter {
public:
    ELogRateLimiter(uint64_t maxMsg = 0, uint64_t timeout = 0,
                    ELogTimeUnits timeoutUnits = ELogTimeUnits::TU_NONE);
    ELogRateLimiter(const ELogRateLimiter&) = delete;
    ELogRateLimiter(ELogRateLimiter&&) = delete;
    ELogRateLimiter& operator=(const ELogRateLimiter&) = delete;
    ~ELogRateLimiter() final {}

    /** @brief Loads filter from configuration. */
    bool load(const ELogConfigMapNode* filterCfg) final;

    /** @brief Loads filter from a free-style predicate-like parsed expression. */
    bool loadExpr(const ELogExpression* expr) final;

    /**
     * @brief Filters a log record.
     * @param logRecord The log record to filter.
     * @return true If the log record is to be logged.
     * @return false If the log record is to be discarded.
     */
    bool filterLogRecord(const ELogRecord& logRecord) final;

protected:
    uint64_t m_maxMsg;
    uint64_t m_timeout;
    ELogTimeUnits m_timeoutUnits;
    uint64_t m_intervalMillis;
    std::atomic<uint64_t> m_currInterval;
    std::atomic<uint64_t> m_currIntervalCount;
    std::atomic<uint64_t> m_prevIntervalCount;

    ELOG_DECLARE_FILTER(ELogRateLimiter, rate_limit)

private:
    bool prepareInterval();
};

}  // namespace elog

#endif  // __ELOG_RATE_LIMITER_H__