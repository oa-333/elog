#ifndef __ELOG_CONFIG_SERVICE_PARAMS_H__
#define __ELOG_CONFIG_SERVICE_PARAMS_H__

#ifdef ELOG_ENABLE_CONFIG_SERVICE

#include <string>

#include "elog_common_def.h"
#include "elog_config_service_publisher.h"
#include "elog_def.h"

namespace elog {

/** @struct Remote Configuration Service parameters. */
struct ELOG_API ELogConfigServiceParams {
    /**
     * @brief Specifies whether the remote configuration service is to be used.
     * This member is valid only when building ELog with ELOG_ENABLE_LIFE_SIGN.
     * By default, if ELOG_ENABLE_LIFE_SIGN is enabled, then life-sign reports are enabled.
     * This flag exists so that users of ELog library that was compiled with
     * ELOG_ENABLE_LIFE_SIGN, would still have the ability to disable life-sign reports.
     */
    bool m_enableConfigService;

    /**
     * @brief The host network interface to listen on for incoming remote configuration service
     * connections. If left empty, then the remote configuration service will listen on all
     * available interfaces. The special values 'localhost' (127.0.0.1), 'primary' (the first
     * non-loopback interface) and 'any'/'all' are accepted. Finally the special format
     * 'name:<interface name>' is also accepted.
     */
    std::string m_configServiceHost;

    /**
     * @brief The port to listen on for incoming remote configuration service connections. If
     * left zero, then any available port will be used, and a publisher will be required to be
     * installed for registering the IP/port in a service registry.
     */
    int m_configServicePort;

    /** @brief Enabled or disables the registered publisher. */
    bool m_enablePublisher;

    /**
     * @brief A custom publisher that will be notified when the service is up or down, and on
     * which interface/port it is listening for incoming connections.
     * @note ELog is not responsible for the life cycle of the publisher.
     */
    ELogConfigServicePublisher* m_publisher;

    ELogConfigServiceParams()
        : m_enableConfigService(ELOG_DEFAULT_ENABLE_CONFIG_SERVICE),
          m_configServiceHost(ELOG_DEFAULT_CONFIG_SERVICE_HOST),
          m_configServicePort(ELOG_DEFAULT_CONFIG_SERVICE_PORT),
          m_enablePublisher(ELOG_DEFAULT_ENABLE_CONFIG_SERVICE_PUBLISH),
          m_publisher(nullptr) {}
    ELogConfigServiceParams(const ELogConfigServiceParams&) = default;
    ELogConfigServiceParams(ELogConfigServiceParams&&) = default;
    ELogConfigServiceParams& operator=(const ELogConfigServiceParams&) = default;
    ~ELogConfigServiceParams() {}
};

}  // namespace elog

#endif  // ELOG_ENABLE_CONFIG_SERVICE

#endif  // __ELOG_CONFIG_SERVICE_PARAMS_H__