#ifndef __ELOG_PARAMS_H__
#define __ELOG_PARAMS_H__

#include <cstdint>
#include <string>

#include "elog_common_def.h"
#include "elog_def.h"
#include "elog_report_handler.h"

#ifdef ELOG_ENABLE_CONFIG_SERVICE
#include "elog_config_service_publisher.h"
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

    /**
     * @brief Specifies the maximum number of threads that are able to concurrently access ELog. If
     * this number is exceeded, then some statistics may not be collected, and the garbage collector
     * used with life-sign reports (and in the experimental group flush policy) would fail to
     * recycle object at some point (i.e. a memory leak is expected).
     * By default, @ref ELOG_DEFAULT_MAX_THREADS is used.
     */
    uint32_t m_maxThreads;

#ifdef ELOG_ENABLE_LIFE_SIGN
    /**
     * @brief Specifies whether life sign reports are to be used.
     * This member is valid only when building ELog with ELOG_ENABLE_LIFE_SIGN.
     * By default, if ELOG_ENABLE_LIFE_SIGN is enabled, then life-sign reports are enabled.
     * This flag exists so that users of ELog library that was compiled with ELOG_ENABLE_LIFE_SIGN,
     * would still have the ability to disable life-sign reports.
     */
    bool m_enableLifeSignReport;

    /**
     * @brief The period in milliseconds of each life-sign background garbage collection task, which
     * wakes up and recycles all objects ready for recycling.
     */
    uint32_t m_lifeSignGCPeriodMillis;

    /**
     * @brief The number of life-sign background garbage collection tasks.
     */
    uint32_t m_lifeSignGCTaskCount;
#endif

#ifdef ELOG_ENABLE_CONFIG_SERVICE
    /**
     * @brief Specifies whether the remote configuration service is to be used.
     * This member is valid only when building ELog with ELOG_ENABLE_LIFE_SIGN.
     * By default, if ELOG_ENABLE_LIFE_SIGN is enabled, then life-sign reports are enabled.
     * This flag exists so that users of ELog library that was compiled with ELOG_ENABLE_LIFE_SIGN,
     * would still have the ability to disable life-sign reports.
     */
    bool m_enableConfigService;

    /**
     * @brief The host network interface to listen on for incoming remote configuration service
     * connections. If left empty, then the remote configuration service will listen on all
     * available interfaces. The special values 'localhost' (127.0.0.1), 'primary' (the first
     * non-loopback interface) and 'any'/'all' are accepted. Finally the special format
     * 'name:<interface name>' is also accepted.
     */
    std::string m_hostInterface;

    /**
     * @brief The port to listen on for incoming remote configuration service connections. If left
     * zero, then any available port will be used, and a publisher will be required to be installed
     * for registering the IP/port in a service registry.
     */
    int m_port;

    /**
     * @brief A custom publisher that will be notified when the service is up or down, and on which
     * interface/port it is listening for incoming connections.
     */
    ELogConfigServicePublisher* m_publisher;
#endif

    ELogParams()
        : m_configFilePath(""),
#ifdef ELOG_ENABLE_RELOAD_CONFIG
          m_reloadPeriodMillis(0),
#endif
          m_reportHandler(nullptr),
          m_reportLevel(ELEVEL_WARN),
          m_maxThreads(ELOG_DEFAULT_MAX_THREADS)
#ifdef ELOG_ENABLE_LIFE_SIGN
          ,
          m_enableLifeSignReport(ELOG_DEFAULT_ENABLE_LIFE_SIGN),
          m_lifeSignGCPeriodMillis(ELOG_DEFAULT_LIFE_SIGN_GC_PERIOD_MILLIS),
          m_lifeSignGCTaskCount(ELOG_DEFAULT_LIFE_SIGN_GC_TASK_COUNT)
#endif
#ifdef ELOG_ENABLE_CONFIG_SERVICE
          ,
          m_enableConfigService(ELOG_DEFAULT_ENABLE_CONFIG_SERVICE),
          m_port(0),
          m_publisher(nullptr)
#endif
    {
    }
    ELogParams(const ELogParams&) = default;
    ELogParams(ELogParams&&) = default;
    ELogParams& operator=(const ELogParams&) = default;
    ~ELogParams() {}
};

}  // namespace elog

#endif  // __ELOG_PARAMS_H__