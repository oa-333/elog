#ifndef __ELOG_PARAMS_H__
#define __ELOG_PARAMS_H__

#include <cstdint>
#include <string>

#include "elog_atomic.h"
#include "elog_common_def.h"
#include "elog_def.h"
#include "elog_rate_limiter.h"
#include "elog_report_handler.h"

#ifdef ELOG_ENABLE_LIFE_SIGN
#include "elog_life_sign_params.h"
#endif

#ifdef ELOG_ENABLE_CONFIG_SERVICE
#include "cfg_srv/elog_config_service_params.h"
#include "cfg_srv/elog_config_service_publisher.h"
#endif

namespace elog {

/** @struct ELog initialization parameters. */
struct ELOG_API ELogParams {
    /**
     * @brief A configuration file path. The file's contents are expected to match the format
     * specified by @ref configureByFile(). By default none is specified.
     */
    std::string m_configFilePath;

#ifdef ELOG_ENABLE_RELOAD_CONFIG
    /**
     * @brief Specifies a configuration reload period in milliseconds.
     * @note Only log levels will be updated. If zero period is specified, then no periodic
     * reloading will take place. By default no periodic reloading takes place.
     */
    uint64_t m_reloadPeriodMillis;
#endif

    /**
     * @brief Specifies a custom handler for ELog internal log message. If none specified, then all
     * internal log messages of the ELog library are sent to the standard output stream, through a
     * dedicated logger under the log source name 'elog'. By default no special handler is provided.
     */
    ELogReportHandler* m_reportHandler;

    /**
     * @brief Sets the log level for internal log messages issued by ELog. By default, WARNING
     * level is used.
     */
    ELogLevel m_reportLevel;

    /** @brief ELog's internal error moderation/rate-limiting parameters. */
    ELogRateLimitParams m_errorModerationRate;

    /**
     * @brief Specifies the maximum number of threads that are able to concurrently access ELog. If
     * this number is exceeded, then some statistics may not be collected, and the garbage collector
     * used with life-sign reports (and in the experimental group flush policy) would fail to
     * recycle object at some point (i.e. a memory leak is expected).
     * By default, @ref ELOG_DEFAULT_MAX_THREADS is used.
     */
    uint32_t m_maxThreads;

    /** @brief Specifies whether log statistics are enabled (per-level counters). */
    ELogAtomic<bool> m_enableLogStatistics;

    /** @brief Specifies whether a time source is used (better performance, less accuracy). */
    ELogAtomic<bool> m_enableTimeSource;

    /** @brief The time source resolution (time stamp update frequency). */
    uint64_t m_timeSourceResolution;

    /** @brief The time source resolution units. */
    ELogTimeUnits m_timeSourceUnits;

#ifdef ELOG_ENABLE_LIFE_SIGN
    ELogLifeSignParams m_lifeSignParams;
#endif

#ifdef ELOG_ENABLE_CONFIG_SERVICE
    ELogConfigServiceParams m_configServiceParams;
#endif

    ELogParams()
        : m_configFilePath(""),
#ifdef ELOG_ENABLE_RELOAD_CONFIG
          m_reloadPeriodMillis(0),
#endif
          m_reportHandler(nullptr),
          m_reportLevel(ELEVEL_WARN),
          m_errorModerationRate(1, ELOG_DEFAULT_ERROR_RATE_SECONDS, ELogTimeUnits::TU_SECONDS),
          m_maxThreads(ELOG_DEFAULT_MAX_THREADS),
          m_enableLogStatistics(ELOG_DEFAULT_ENABLE_LOG_STATISTICS),
          m_enableTimeSource(ELOG_DEFAULT_ENABLE_TIME_SOURCE),
          m_timeSourceResolution(ELOG_DEFAULT_TIME_SOURCE_RESOLUTION),
          m_timeSourceUnits(ELOG_DEFAULT_TIME_SOURCE_UNITS) {
    }
    ELogParams(const ELogParams&) = default;
    ELogParams(ELogParams&&) = default;
    ELogParams& operator=(const ELogParams&) = default;
    ~ELogParams() {}
};

}  // namespace elog

#endif  // __ELOG_PARAMS_H__