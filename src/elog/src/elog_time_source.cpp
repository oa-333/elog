#include "elog_time_source.h"

#include <cinttypes>

#include "elog_common.h"
#include "elog_report.h"

/** @def DEfault time resolution (milliseconds). */
#define ELOG_DEFAULT_TIME_RESOLUTION_MILLIS 100ull

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogTimeSource)

void ELogTimeSource::initialize(uint64_t resolution, ELogTimeUnits resolutionUnits) {
    if (!convertTimeUnit(resolution, resolutionUnits, ELogTimeUnits::TU_NANO_SECONDS,
                         m_resolutionNanos)) {
        ELOG_REPORT_WARN("Invalid time source resolution, using default resolution: %" PRIu64
                         " milliseconds",
                         ELOG_DEFAULT_TIME_RESOLUTION_MILLIS);
        m_resolutionNanos = ELOG_DEFAULT_TIME_RESOLUTION_MILLIS * 1000 * 1000;
    }
}

void ELogTimeSource::start() {
    m_stop.store(false, std::memory_order_release);
    updateCurrentTime();
    m_updateTimeTask = std::thread(&ELogTimeSource::updateTimeTask, this);
}

void ELogTimeSource::stop() {
    m_stop.store(true, std::memory_order_release);
    m_updateTimeTask.join();
}

void ELogTimeSource::updateTimeTask() {
    while (!m_stop.load(std::memory_order_relaxed)) {
        updateCurrentTime();
        std::this_thread::sleep_for(std::chrono::nanoseconds(m_resolutionNanos));
    }
}

void ELogTimeSource::updateCurrentTime() {
    ELogTime currentTime;
    elogGetCurrentTime(currentTime);
    m_currentTime.store(currentTime, std::memory_order_relaxed);
}

}  // namespace elog