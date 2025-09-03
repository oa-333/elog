#include "elog_life_sign_filter.h"

#ifdef ELOG_ENABLE_LIFE_SIGN

#include "elog_filter.h"
#include "elog_rate_limiter.h"
#include "elog_report.h"

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogLifeSignFilter)

ELogLifeSignFilter::ELogLifeSignFilter() {
    for (uint32_t i = 0; i < ELEVEL_COUNT; ++i) {
        m_levelFilters[i] = nullptr;
    }
}

bool ELogLifeSignFilter::setLevelFilter(ELogLevel level, const ELogFrequencySpec& frequencySpec,
                                        ELogFilter*& prevFilter) {
    ELogFilter* newFilter = makeLifeSignFilter(frequencySpec);
    if (newFilter == nullptr) {
        return false;
    }
    prevFilter = m_levelFilters[level].load(std::memory_order_acquire);
    m_levelFilters[level].store(newFilter, std::memory_order_release);
    // NOTE: caller is responsible for retiring old filter to GC
    return true;
}

ELogFilter* ELogLifeSignFilter::removeLevelFilter(ELogLevel level) {
    ELogFilter* prevFilter = m_levelFilters[level].load(std::memory_order_acquire);
    m_levelFilters[level].store(nullptr, std::memory_order_release);
    // NOTE: caller is responsible for retiring old filter to GC
    return prevFilter;
}

bool ELogLifeSignFilter::filterLogRecord(const ELogRecord& logRecord) {
    // NOTE: caller is responsible for doing this safely with epoch incremented first
    ELogFilter* filter = m_levelFilters[logRecord.m_logLevel].load(std::memory_order_relaxed);
    return (filter == nullptr || filter->filterLogRecord(logRecord));
}

ELogFilter* ELogLifeSignFilter::makeLifeSignFilter(const ELogFrequencySpec& frequencySpec) {
    ELogFilter* filter = nullptr;
    if (frequencySpec.m_method == ELogFrequencySpecMethod::FS_EVERY_N_MESSAGES) {
        filter = new (std::nothrow) ELogCountFilter(frequencySpec.m_msgCount);
    } else {
        filter = new (std::nothrow) ELogRateLimiter(
            frequencySpec.m_msgCount, frequencySpec.m_timeout, frequencySpec.m_timeoutUnits);
    }
    if (filter == nullptr) {
        ELOG_REPORT_ERROR("Failed to allocate life-sign filter, out of memory");
    }
    return filter;
}

}  // namespace elog

#endif  // ELOG_ENABLE_LIFE_SIGN