#include "elog_rate_limiter.h"

#include <cinttypes>

namespace elog {

bool ELogRateLimiter::filterLogRecord(const ELogRecord& logRecord) {
    // we get a timestamp and associate it with a whole second boundary
    // next we check the current count. If it is associated with the current whole second, then we
    // just increase current second count. otherwise, it is associated with a previous whole second,
    // then there are two options: (1) it belongs to the previous second, and so we just set the
    // previous second count and the whole second with which it is associated. (2) it belongs to a
    // too far previous whole second, in which case the previous second count is reset to zero.
    tstamp_t currTstamp = getTstamp();
    uint64_t wholeSecond = currTstamp.count() / 1000;
    uint64_t currSecond = m_currSecond.load(std::memory_order_relaxed);
    // fprintf(stderr, "whole second = %" PRIu64 ", curr second = %" PRIu64 "\n", wholeSecond,
    //         currSecond);
    if (currSecond == wholeSecond) {
        // compute sliding window rate
        uint64_t prevCount = m_prevSecondCount.load(std::memory_order_relaxed);
        uint64_t currCount = m_currSecondCount.load(std::memory_order_relaxed);
        uint64_t count = prevCount * (1000 - (wholeSecond % 1000)) / 1000.0f + currCount;
        if (count < m_maxMsgPerSecond) {
            // NOTE: there might be a small breach here (due to possible sudden thundering herd),
            // but we are ok with that, because this is not a strict rate limiter
            // fprintf(stderr, "count=%u, prevCount=%u, currCount=%u, sec-part=%u\n",
            // (unsigned)count,
            //        (unsigned)prevCount, (unsigned)currCount, (unsigned)(wholeSecond % 1000));
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
            // fprintf(stderr, "Whole second passed, moving %u count back\n",
            //         (unsigned)currSecondCount);
        } else {
            m_prevSecondCount.store(0, std::memory_order_relaxed);
            // fprintf(stderr, "Whole second jumped\n");
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
