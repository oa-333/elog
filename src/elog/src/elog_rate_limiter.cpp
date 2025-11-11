#include "elog_rate_limiter.h"

#include <cinttypes>

#include "elog_common.h"
#include "elog_report.h"

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogRateLimitFilter)

ELOG_IMPLEMENT_FILTER(ELogRateLimitFilter)

ELogRateLimitFilter::ELogRateLimitFilter(uint64_t maxMsg /* = 0 */, uint64_t timeout /* = 0 */,
                                         ELogTimeUnits timeoutUnits /* = ELogTimeUnits::TU_NONE */)
    : ELogCmpFilter(ELogCmpOp::CMP_OP_EQ),
      m_maxMsg(maxMsg),
      m_timeout(timeout),
      m_timeoutUnits(timeoutUnits),
      m_intervalMillis(0),
      m_currIntervalId(0),
      m_currIntervalCount(0),
      m_prevIntervalCount(0) {
    if (m_timeout != 0 && m_timeoutUnits != ELogTimeUnits::TU_NONE) {
        prepareInterval();
    }
}

ELogRateLimitFilter::ELogRateLimitFilter(const ELogRateLimitParams& params)
    : ELogCmpFilter(ELogCmpOp::CMP_OP_EQ),
      m_maxMsg(params.m_maxMsgs),
      m_timeout(params.m_timeout),
      m_timeoutUnits(params.m_units),
      m_intervalMillis(0),
      m_currIntervalId(0),
      m_currIntervalCount(0),
      m_prevIntervalCount(0) {
    if (m_timeout != 0 && m_timeoutUnits != ELogTimeUnits::TU_NONE) {
        prepareInterval();
    }
}

bool ELogRateLimitFilter::prepareInterval() {
    if (!convertTimeUnit(m_timeout, m_timeoutUnits, ELogTimeUnits::TU_MILLI_SECONDS,
                         m_intervalMillis)) {
        ELOG_REPORT_ERROR("Invalid rate limiter timeout value: %" PRIu64 " %s", m_timeout,
                          timeUnitToString(m_timeoutUnits));
        return false;
    }

    if (m_intervalMillis == 0) {
        ELOG_REPORT_ERROR(
            "Rate limiter timeout less than 1 millisecond truncated to zero value: %" PRIu64 " %s",
            m_timeout, timeUnitToString(m_timeoutUnits));
        return false;
    }

    return true;
}

bool ELogRateLimitFilter::load(const ELogConfigMapNode* filterCfg) {
    if (!loadIntFilter(filterCfg, "rate", "max_msg", m_maxMsg)) {
        return false;
    }

    if (!loadTimeoutFilter(filterCfg, "rate", "timeout", m_timeout, m_timeoutUnits,
                           ELogTimeUnits::TU_NONE)) {
        return false;
    }
    if (!prepareInterval()) {
        return false;
    }
    return true;
}

bool ELogRateLimitFilter::loadExpr(const ELogExpression* expr) {
    if (expr->m_type != ELogExpressionType::ET_FUNC_EXPR) {
        ELOG_REPORT_ERROR(
            "Cannot load rate limiter, invalid expression type (required function expression)");
        return false;
    }
    const ELogFunctionExpression* funcExpr = (const ELogFunctionExpression*)expr;
    if (funcExpr->m_expressions.size() != 2) {
        ELOG_REPORT_ERROR(
            "Cannot load rate limiter, function expression must contain exactly two "
            "sub-expressions");
        return false;
    }
    if (!loadIntFilter(funcExpr->m_expressions[0], "rate", m_maxMsg, "max_msg")) {
        return false;
    }
    if (!loadTimeoutFilter(funcExpr->m_expressions[1], "rate", m_timeout, m_timeoutUnits,
                           ELogTimeUnits::TU_NONE, "timeout")) {
        return false;
    }
    if (!prepareInterval()) {
        return false;
    }
    return true;
}

// TODO: consider providing several types of rate limiters
bool ELogRateLimitFilter::filterLogRecord(const ELogRecord& logRecord) {
    // this is an interpolation-based rate-limiter
    // within each continuous time interval an estimation is made whether another call can be made.
    // the method to do so is by dividing time to whole intervals. When the rate limiter is
    // consulted, it checks whether the number of samples in the current interval added to the
    // estimated number of samples in the previous interval (according to the portion of the
    // interval left) allows for a call to be made.
    // in simpler words, assume I is the interval size and T is the timestamp, then we have:
    //
    //      i = T / I    ==> absolute interval association
    //      S(i)         ==> Sample count in interval i
    //      t = T % I    ==> current interval portion
    //      1 - t        ==> previous interval portion
    //
    // so we compute, the continuous estimated sample count:
    //
    //      S(i) + S(i-1) * (1-t) / I
    //
    // if this sum exceeds the rate limit then the call is denied, otherwise it is granted.
    //
    // pay attention that sometimes the previous interval may have no samples at all
    // this situation is detected by comparing absolute interval ids

    // guard against bad construction
    if (m_intervalMillis == 0) {
        return true;
    }

    // take time stamp from steady/monotonic clock to avoid negative time diffs
    uint64_t tstamp = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now().time_since_epoch())
                          .count();

    // we are not expecting negative value here
    uint64_t wholeInterval = tstamp / m_intervalMillis;
    uint64_t currIntervalId = m_currIntervalId.load(std::memory_order_acquire);
    if (currIntervalId == wholeInterval) {
        // compute sliding window rate
        uint64_t prevCount = m_prevIntervalCount.load(std::memory_order_relaxed);
        uint64_t currCount = m_currIntervalCount.load(std::memory_order_relaxed);
        // NOTE: we do linear interpolation to estimate the amount of messages in the sliding window
        // part covering the previous interval. no interpolation is required for current interval
        // message count, since it is being currently counted
        // NOTE: carefully compute, first multiply, then divide, otherwise value is truncated
        uint64_t currIntervalPortion = tstamp % m_intervalMillis;
        uint64_t prevIntervalPortion = m_intervalMillis - currIntervalPortion;
        uint64_t estimatedCount = prevCount * prevIntervalPortion / m_intervalMillis + currCount;
        if (estimatedCount < m_maxMsg) {
            // NOTE: there might be a small breach here (due to possible sudden thundering herd),
            // but we are ok with that, because this is not a strict rate limiter
            m_currIntervalCount.fetch_add(1, std::memory_order_release);
            return true;
        } else {
            return false;
        }
    }

    // a whole interval passed
    // NOTE: we CAS here to avoid race conditions, first wins, the others do nothing, in the expense
    // of slight inaccuracy (since they should have increased the current interval count)
    if (m_currIntervalId.compare_exchange_strong(currIntervalId, wholeInterval,
                                                 std::memory_order_seq_cst)) {
        if (currIntervalId == (wholeInterval - 1)) {
            // store current interval count in previous interval
            uint64_t currIntervalCount = m_currIntervalCount.load(std::memory_order_relaxed);
            m_prevIntervalCount.store(currIntervalCount, std::memory_order_release);
        } else {
            // otherwise previous interval is zero
            m_prevIntervalCount.store(0, std::memory_order_relaxed);
        }
        // in any case we count first sample in current interval
        m_currIntervalCount.store(1, std::memory_order_release);
    }
    return true;
}

ELogRateLimiter::ELogRateLimiter(uint64_t maxMsg /* = 0 */, uint64_t timeout /* = 0 */,
                                 ELogTimeUnits timeoutUnits /* = ELogTimeUnits::TU_NONE */)
    : m_filter(nullptr) {
    m_filter = new (m_rateLimiterBuf) ELogRateLimitFilter(maxMsg, timeout, timeoutUnits);
}

ELogRateLimiter::ELogRateLimiter(const ELogRateLimitParams& params) : m_filter(nullptr) {
    m_filter = new (m_rateLimiterBuf) ELogRateLimitFilter(params);
}

ELogRateLimiter::~ELogRateLimiter() {
    m_filter->terminate();
    m_filter = nullptr;
}

}  // namespace elog
