#ifndef __ELOG_LIFE_SIGN_FILTER_H__
#define __ELOG_LIFE_SIGN_FILTER_H__

#ifdef ELOG_ENABLE_LIFE_SIGN

#include <atomic>

#include "elog_common_def.h"
#include "elog_record.h"

namespace elog {

class ELOG_API ELogFilter;

class ELOG_API ELogLifeSignFilter {
public:
    ELogLifeSignFilter();
    ELogLifeSignFilter(const ELogLifeSignFilter&) = delete;
    ELogLifeSignFilter(ELogLifeSignFilter&&) = delete;
    ELogLifeSignFilter& operator=(const ELogLifeSignFilter&) = delete;
    ~ELogLifeSignFilter() {}

    /**
     * @brief Sets the life-sign report frequency for the given log level.
     * @param level The log level.
     * @param frequencySpec The required frequency.
     * @param prevFilter The previously installed filter. Caller is responsible for handing over
     * this filter to the life-sign garbage collector.
     * @return The operation's result.
     */
    bool setLevelFilter(ELogLevel level, const ELogFrequencySpec& frequencySpec,
                        ELogFilter*& prevFilter);

    /**
     * @brief Removes the life-sign report filter for the given log level.
     * @param level The log level.
     * @return The previously installed filter if any, for the given log level.
     */
    ELogFilter* removeLevelFilter(ELogLevel level);

    /** @brief Queries whether a filter was set for the specified log level. */
    inline bool hasLevelFilter(ELogLevel level) const { return m_levelFilters[level] != nullptr; }

    /** @brief Queries whether a log record should be reported to the life-sign manager. */
    bool filterLogRecord(const ELogRecord& logRecord);

private:
    std::atomic<ELogFilter*> m_levelFilters[ELEVEL_COUNT];

    ELogFilter* makeLifeSignFilter(const ELogFrequencySpec& frequencySpec);
};

}  // namespace elog

#endif  // ELOG_ENABLE_LIFE_SIGN

#endif  // __ELOG_LIFE_SIGN_FILTER_H__