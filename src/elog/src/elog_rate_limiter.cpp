#include "elog_rate_limiter.h"

#include <cinttypes>

#include "elog_error.h"

namespace elog {

ELOG_IMPLEMENT_FILTER(ELogRateLimiter);

bool ELogRateLimiter::load(const std::string& logTargetCfg,
                           const ELogTargetNestedSpec& logTargetSpec) {
    ELogPropertyMap::const_iterator itr = logTargetSpec.m_spec.m_props.find("max_msg_per_sec");
    if (itr == logTargetSpec.m_spec.m_props.end()) {
        ELOG_REPORT_ERROR(
            "Invalid rate limiter configuration, missing expected max_msg_per_sec property: %s",
            logTargetCfg.c_str());
        return false;
    }
    if (!parseIntProp("max_msg_per_sec", logTargetCfg, itr->second, m_maxMsgPerSecond, true)) {
        ELOG_REPORT_ERROR(
            "Invalid rate limit configuration, property 'max_msg_per_sec value '%s' is an "
            "ill-formed integer: %s",
            itr->second.c_str(), logTargetCfg.c_str());
        return false;
    }
    return true;
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
    uint64_t wholeSecond = currTstamp.count() / 1000;
    uint64_t currSecond = m_currSecond.load(std::memory_order_relaxed);
    if (currSecond == wholeSecond) {
        // compute sliding window rate
        uint64_t prevCount = m_prevSecondCount.load(std::memory_order_relaxed);
        uint64_t currCount = m_currSecondCount.load(std::memory_order_relaxed);
        uint64_t count = prevCount * (1000 - (wholeSecond % 1000)) / 1000.0f + currCount;
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
