#ifndef __ELOG_TIME_SOURCE_H__
#define __ELOG_TIME_SOURCE_H__

#include <atomic>
#include <thread>

#include "elog_common_def.h"
#include "elog_def.h"
#include "elog_time.h"

namespace elog {

/**
 * @brief A lazy time source that provides the current time. The internal timestamp is periodically
 * updated by a background task, so that taking timestamp does not affect performance that much.
 */
class ELogTimeSource {
public:
    ELogTimeSource() : m_resolutionNanos(0), m_stop(false) {}
    ELogTimeSource(const ELogTimeSource&) = delete;
    ELogTimeSource(ELogTimeSource&&) = delete;
    ELogTimeSource& operator=(const ELogTimeSource&) = delete;
    ~ELogTimeSource() {}

    /** @brief Configures the time source. */
    void initialize(uint64_t resolution, ELogTimeUnits resolutionUnits);

    /** @brief Starts the time source running. */
    void start();

    /** @brief Stops the time source. */
    void stop();

    /** @brief Retrieves the current timestamp. */
    inline void getCurrentTime(ELogTime& currentTime) {
        currentTime = m_currentTime.load(std::memory_order_relaxed);
    }

private:
    uint64_t m_resolutionNanos;
    std::atomic<ELogTime> m_currentTime;
    std::thread m_updateTimeTask;
    std::atomic<bool> m_stop;

    void updateTimeTask();
    void updateCurrentTime();
};

}  // namespace elog

#endif  // __ELOG_TIME_SOURCE_H__