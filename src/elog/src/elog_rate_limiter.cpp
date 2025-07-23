#include "elog_rate_limiter.h"

#include <cinttypes>

#include "elog_common.h"
#include "elog_report.h"

namespace elog {

ELOG_IMPLEMENT_FILTER(ELogRateLimiter);

bool ELogRateLimiter::load(const ELogConfigMapNode* filterCfg) {
    return loadIntFilter(filterCfg, "max_msg_per_sec", "rate limiter", m_maxMsgPerSecond);
}

bool ELogRateLimiter::loadExpr(const ELogExpression* expr) {
    return loadIntFilter(expr, "max_msg_per_sec", m_maxMsgPerSecond);
}

// TODO: consider providing several types of rate limiters
bool ELogRateLimiter::filterLogRecord(const ELogRecord& logRecord) {
    // we get a timestamp and associate it with a whole second boundary
    // next we check the current count. If it is associated with the current whole second, then we
    // just increase current second count. otherwise, it is associated with a previous whole second,
    // then there are two options: (1) it belongs to the previous second, and so we just set the
    // previous second count and the whole second with which it is associated. (2) it belongs to a
    // too far previous whole second, in which case the previous second count is reset to zero.
    tstamp_t currTstamp = getTstamp();
    if (currTstamp.count() < 0) {
        // something went bad, we have a negative timestamp, so we let the record be logged
        return true;
    }
    // we are not expecting negative value here
    uint64_t wholeSecond = (uint64_t)(currTstamp.count() / 1000);
    uint64_t currSecond = m_currSecond.load(std::memory_order_relaxed);
    if (currSecond == wholeSecond) {
        // compute sliding window rate
        uint64_t prevCount = m_prevSecondCount.load(std::memory_order_relaxed);
        uint64_t currCount = m_currSecondCount.load(std::memory_order_relaxed);
        // carefully compute, first multiply, then divide, otherwise value is truncated
        uint64_t count = prevCount * (1000 - (wholeSecond % 1000));
        count = count / 1000ull + currCount;
        if (count < m_maxMsgPerSecond) {
            // NOTE: there might be a small breach here (due to possible sudden thundering herd),
            // but we are ok with that, because this is not a strict rate limiter
            m_currSecondCount.fetch_add(1, std::memory_order_relaxed);
            return true;
        } else {
            return false;
        }
    } else {
        // a whole second passed
        if (currSecond == (wholeSecond - 1)) {
            uint64_t currSecondCount = m_currSecondCount.load(std::memory_order_relaxed);
            m_prevSecondCount.store(currSecondCount, std::memory_order_relaxed);
        } else {
            m_prevSecondCount.store(0, std::memory_order_relaxed);
        }
        m_currSecondCount.store(1, std::memory_order_relaxed);
        m_currSecond.store(wholeSecond, std::memory_order_relaxed);
        return true;
    }
}

ELogRateLimiter::tstamp_t ELogRateLimiter::getTstamp() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch());
}

}  // namespace elog
