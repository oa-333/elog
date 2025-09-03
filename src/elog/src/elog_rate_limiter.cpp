#include "elog_rate_limiter.h"

#include <cinttypes>

#include "elog_common.h"
#include "elog_report.h"

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogRateLimiter)

ELOG_IMPLEMENT_FILTER(ELogRateLimiter)

ELogRateLimiter::ELogRateLimiter(uint64_t maxMsg /* = 0 */, uint64_t timeout /* = 0 */,
                                 ELogTimeUnits timeoutUnits /* = ELogTimeUnits::TU_NONE */)
    : ELogCmpFilter(ELogCmpOp::CMP_OP_EQ),
      m_maxMsg(maxMsg),
      m_timeout(timeout),
      m_timeoutUnits(timeoutUnits),
      m_intervalMillis(0),
      m_currInterval(0),
      m_currIntervalCount(0),
      m_prevIntervalCount(0) {
    if (m_timeout != 0 && m_timeoutUnits != ELogTimeUnits::TU_NONE) {
        prepareInterval();
    }
}

bool ELogRateLimiter::prepareInterval() {
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

bool ELogRateLimiter::load(const ELogConfigMapNode* filterCfg) {
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

bool ELogRateLimiter::loadExpr(const ELogExpression* expr) {
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
bool ELogRateLimiter::filterLogRecord(const ELogRecord& logRecord) {
    // TODO: fix this comment, it is too unclear
    // we get a sample timestamp and associate it with a whole interval boundary
    // next we check the current count. If it is associated with the current whole interval, then we
    // check for rate limit, and if ok we just increase current interval count. otherwise, it is
    // associated with a previous whole interval, then there are two options: (1) it belongs to the
    // previous interval, and so we just set the previous interval count and the whole interval with
    // which it is associated. (2) it belongs to a too far previous whole interval, in which case
    // the previous interval count is reset to zero.

    // guard against bad construction
    if (m_intervalMillis == 0) {
        return true;
    }

    tstamp_t currTstamp = getTstamp();
    if (currTstamp.count() < 0) {
        // something went bad, we have a negative timestamp, so we let the record be logged
        return true;
    }
    uint64_t tstamp = (uint64_t)currTstamp.count();

    // we are not expecting negative value here
    uint64_t wholeInterval = tstamp / m_intervalMillis;
    uint64_t currInterval = m_currInterval.load(std::memory_order_acquire);
    if (currInterval == wholeInterval) {
        // compute sliding window rate
        uint64_t prevCount = m_prevIntervalCount.load(std::memory_order_relaxed);
        uint64_t currCount = m_currIntervalCount.load(std::memory_order_relaxed);
        // NOTE: we do linear interpolation to estimate the amount of messages in the sliding window
        // part covering the previous interval. no interpolation is required for current interval
        // message count, since it is being currently counted
        // NOTE: carefully compute, first multiply, then divide, otherwise value is truncated
        uint64_t count =
            prevCount * (m_intervalMillis - (tstamp % m_intervalMillis)) / m_intervalMillis;
        count += currCount;
        if (count < m_maxMsg) {
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
    if (m_currInterval.compare_exchange_strong(currInterval, wholeInterval,
                                               std::memory_order_seq_cst)) {
        if (currInterval == (wholeInterval - 1)) {
            // store current interval count in previous interval
            uint64_t currIntervalCount = m_currIntervalCount.load(std::memory_order_relaxed);
            m_prevIntervalCount.store(currIntervalCount, std::memory_order_release);
        } else {
            // otherwise previous interval is zero
            m_prevIntervalCount.store(0, std::memory_order_relaxed);
        }
        // in any case we put
        m_currIntervalCount.store(1, std::memory_order_release);
    }
    return true;
}

ELogRateLimiter::tstamp_t ELogRateLimiter::getTstamp() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch());
}

}  // namespace elog
