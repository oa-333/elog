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

/** @brief Rate limit parameters. */
struct ELOG_API ELogRateLimitParams {
    /**
     * @brief Maximum number of messages allowed to pass through the rate limiter in the given time
     * frame.
     */
    uint64_t m_maxMsgs;

    /** @brief The timeout used for each rate limit window.  */
    uint64_t m_timeout;

    /** @brief The timeout units. */
    ELogTimeUnits m_units;

    ELogRateLimitParams(uint64_t maxMsg = 0, uint64_t timeout = 0,
                        ELogTimeUnits units = ELogTimeUnits::TU_NONE)
        : m_maxMsgs(maxMsg), m_timeout(timeout), m_units(units) {}
    ELogRateLimitParams(const ELogRateLimitParams&) = default;
    ELogRateLimitParams(ELogRateLimitParams&&) = default;
    ELogRateLimitParams& operator=(const ELogRateLimitParams&) = default;
    ~ELogRateLimitParams() {}
};

/** @brief Log rate limit filter. */
class ELOG_API ELogRateLimitFilter final : public ELogCmpFilter {
public:
    ELogRateLimitFilter(uint64_t maxMsg = 0, uint64_t timeout = 0,
                        ELogTimeUnits timeoutUnits = ELogTimeUnits::TU_NONE);
    ELogRateLimitFilter(const ELogRateLimitParams& params);
    ELogRateLimitFilter(const ELogRateLimitFilter&) = delete;
    ELogRateLimitFilter(ELogRateLimitFilter&&) = delete;
    ELogRateLimitFilter& operator=(const ELogRateLimitFilter&) = delete;

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

    ELOG_DECLARE_FILTER(ELogRateLimitFilter, rate_limit, ELOG_API)

private:
    bool prepareInterval();
};

/** Rate limiter utility class, without ELogFilter's stuff. */
class ELOG_API ELogRateLimiter {
public:
    ELogRateLimiter(uint64_t maxMsg = 0, uint64_t timeout = 0,
                    ELogTimeUnits timeoutUnits = ELogTimeUnits::TU_NONE);
    ELogRateLimiter(const ELogRateLimitParams& params);
    ELogRateLimiter(const ELogRateLimiter&) = delete;
    ELogRateLimiter(ELogRateLimiter&&) = delete;
    ELogRateLimiter& operator=(const ELogRateLimiter&) = delete;
    ~ELogRateLimiter();

    /**
     * @brief Filters a log record.
     * @param logRecord The log record to filter.
     * @return true If the log record is to be logged.
     * @return false If the log record is to be discarded.
     */
    inline bool filterLogRecord(const ELogRecord& logRecord) {
        return m_filter->filterLogRecord(logRecord);
    }

private:
    // buffer for in-place allocation
    char m_rateLimiterBuf[sizeof(ELogRateLimitFilter)];
    ELogRateLimitFilter* m_filter;
};

/** @brief Helper class for implementing "moderate" macros. */
class ELOG_API ELogModerate {
public:
    ELogModerate(const char* fmt, uint64_t maxMsgs, uint64_t timeout, ELogTimeUnits units)
        : m_fmt(fmt),
          m_rateLimiter(maxMsgs, timeout, units),
          m_discardCount(0),
          m_isDiscarding(false),
          m_startDiscardCount(0) {}
    ELogModerate(const char* fmt, const ELogRateLimitParams& params)
        : m_fmt(fmt),
          m_rateLimiter(params),
          m_discardCount(0),
          m_isDiscarding(false),
          m_startDiscardCount(0) {}
    ELogModerate(const ELogModerate&) = delete;
    ELogModerate(ELogModerate&&) = delete;
    ELogModerate& operator=(ELogModerate&) = delete;
    ~ELogModerate() {}

    /** @brief Moderate call. Returns true if call can be made. */
    bool moderate();

private:
    const char* m_fmt;
    ELogRateLimiter m_rateLimiter;
    static ELogRecord m_dummy;
    std::atomic<uint64_t> m_discardCount;
    std::atomic<bool> m_isDiscarding;
    std::chrono::steady_clock::time_point m_startDiscardTime;
    uint64_t m_startDiscardCount;
};

}  // namespace elog

#endif  // __ELOG_RATE_LIMITER_H__