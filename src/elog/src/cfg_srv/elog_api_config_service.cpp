#include "cfg_srv/elog_api_config_service.h"

#ifdef ELOG_ENABLE_CONFIG_SERVICE

#include "elog_api.h"
#include "elog_config_service.h"
#include "elog_internal.h"
#include "elog_report.h"

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogConfigServiceApi)

static bool loadConfigService(const ELogPropertySequence& props,
                              std::string& configServicePublisher);
static bool loadConfigService(const ELogConfigMapNode* cfgMap, std::string& configServicePublisher);
static bool loadConfigServiceFromEnv(const ELogPropertySequence* props,
                                     const ELogConfigMapNode* cfgMap,
                                     std::string& configServicePublisher);
static bool loadConfigServicePublisher(const char* configServicePublisher,
                                       const ELogPropertySequence* props,
                                       const ELogConfigMapNode* cfgMap);
static void updatePublisher(ELogConfigServicePublisher* publisher, bool isInternalPublisher);

inline void removePublisher() { updatePublisher(nullptr, false); }

// atomic flag to make sure we load from environment variables only once, otherwise it is impossible
// to override environment variables settings from configuration file changes
// the scenario targeted here is as follows:
//
// - production environment has some configuration file, but there are problems
// - customer does not allow changing any configuration file
// - customer's application can be restarted with updated env vars that locally override
//   configuration settings
//
// Conversely, after overriding configuration from env, it should be possible to update
// configuration from file and see effect, so this flag is required to ensure when reloading
// configuration the second time and after that, env settings are not considered
static std::atomic<bool> sUpdatedFromEnv = false;

// normally, the caller is responsible for managing the life cycle of the installed publisher, since
// it is an external component. nevertheless, it may be possible that ELog itself installs a
// publisher that is loaded from configuration, in which case the publisher should be deleted by
// ELog. in order to distinguish between the cases, this flag exists.
static std::atomic<bool> sShouldDeletePublisher = false;

bool initConfigService() {
    // create service
    if (!ELogConfigService::createInstance()) {
        return false;
    }

    // initialize service
    const ELogConfigServiceParams& configServiceParams = getParams().m_configServiceParams;
    ELogConfigService* configService = ELogConfigService::getInstance();
    if (configService->initialize(configServiceParams.m_configServiceHost.c_str(),
                                  configServiceParams.m_configServicePort,
                                  configServiceParams.m_publisher) != commutil::ErrorCode::E_OK) {
        ELogConfigService::destroyInstance();
        return false;
    }

    // start service running
    if (configService->start() != commutil::ErrorCode::E_OK) {
        configService->terminate();
        ELogConfigService::destroyInstance();
        return false;
    }
    return true;
}

void termConfigService() {
    // first stop the configuration service (as it might be accessing the publisher)
    ELogConfigService* configService = ELogConfigService::getInstance();
    if (configService != nullptr) {
        configService->stop();
        configService->terminate();
        ELogConfigService::destroyInstance();
    }

    // now we can destroy the publisher
    removePublisher();
}

bool configConfigServiceProps(const ELogPropertySequence& props) {
    // load remote configuration service from configuration node and put in global parameters
    // NOTE: port is optional

    // NOTE: since we don't have any good way to tell what changed (maybe even switch publisher
    // type) we stop the service, set all parameters and then restart
    bool isRunning = ELogConfigService::getInstance()->isRunning();
    if (isRunning) {
        if (!stopConfigService()) {
            ELOG_REPORT_ERROR("Failed to stop configuration service for configuration update");
            return false;
        }
    }

    std::string configServicePublisher;
    bool res = loadConfigService(props, configServicePublisher);
    if (res) {
        res = loadConfigServiceFromEnv(&props, nullptr, configServicePublisher);
    }
    if (res && !configServicePublisher.empty()) {
        res = loadConfigServicePublisher(configServicePublisher.c_str(), &props, nullptr);
    }

    if (isRunning) {
        if (!startConfigService()) {
            ELOG_REPORT_ERROR("Failed to restart configuration service after configuration update");
            return false;
        }
    }

    return res;
}

bool configConfigService(const ELogConfigMapNode* cfgMap) {
    // load remote configuration service from configuration node and put in global parameters
    // NOTE: port is optional

    // NOTE: since we don't have any good way to tell what changed (maybe even switch publisher
    // type) we stop the service, set all parameters and then restart
    bool isRunning = ELogConfigService::getInstance()->isRunning();
    if (isRunning) {
        if (!stopConfigService()) {
            ELOG_REPORT_ERROR("Failed to stop configuration service for configuration update");
            return false;
        }
    }

    std::string configServicePublisher;
    bool res = loadConfigService(cfgMap, configServicePublisher);
    if (res) {
        res = loadConfigServiceFromEnv(nullptr, cfgMap, configServicePublisher);
    }
    if (res && !configServicePublisher.empty()) {
        res = loadConfigServicePublisher(configServicePublisher.c_str(), nullptr, cfgMap);
    }

    if (isRunning) {
        if (!startConfigService()) {
            ELOG_REPORT_ERROR("Failed to restart configuration service after configuration update");
            return false;
        }
    }

    return res;
}

bool loadConfigService(const ELogPropertySequence& props, std::string& configServicePublisher) {
    // load remote configuration service from properties and put in global parameters
    ELogConfigServiceParams& params = modifyParams().m_configServiceParams;
    if (!getBoolProp(props, ELOG_ENABLE_CONFIG_SERVICE_NAME, params.m_enableConfigService)) {
        return false;
    }

    // NOTE: port is optional
    if (!getProp(props, ELOG_CONFIG_SERVICE_INTERFACE_NAME, params.m_configServiceHost)) {
        return false;
    }
    if (!getIntProp(props, ELOG_CONFIG_SERVICE_PORT_NAME, params.m_configServicePort)) {
        return false;
    }

    // now check for publisher
    if (!getBoolProp(props, ELOG_ENABLE_CONFIG_SERVICE_PUBLISHER_NAME, params.m_enablePublisher)) {
        return false;
    }
    getProp(props, ELOG_CONFIG_SERVICE_PUBLISHER_NAME, configServicePublisher);
    return true;
}

bool loadConfigService(const ELogConfigMapNode* cfgMap, std::string& configServicePublisher) {
    ELogConfigServiceParams& params = modifyParams().m_configServiceParams;
    bool found = false;
    if (!cfgMap->getBoolValue(ELOG_ENABLE_CONFIG_SERVICE_NAME, found,
                              params.m_enableConfigService)) {
        return false;
    }
    if (!cfgMap->getStringValue(ELOG_CONFIG_SERVICE_INTERFACE_NAME, found,
                                params.m_configServiceHost)) {
        return false;
    }

    int64_t configServicePort64 = 0;
    if (!cfgMap->getIntValue(ELOG_CONFIG_SERVICE_PORT_NAME, found, configServicePort64)) {
        return false;
    }
    if (found) {
        if (configServicePort64 < 0 || configServicePort64 > INT_MAX) {
            ELOG_REPORT_ERROR("Invalid port value %" PRId64
                              " specified for %s, out of valid range [0, %d]",
                              configServicePort64, ELOG_CONFIG_SERVICE_PORT_NAME, (int)INT_MAX);
            return false;
        }
        params.m_configServicePort = (int)configServicePort64;
    }

    // check for publisher
    if (!cfgMap->getBoolValue(ELOG_ENABLE_CONFIG_SERVICE_PUBLISHER_NAME, found,
                              params.m_enablePublisher)) {
        return false;
    }
    if (!cfgMap->getStringValue(ELOG_CONFIG_SERVICE_PUBLISHER_NAME, found,
                                configServicePublisher)) {
        return false;
    }
    return true;
}

bool loadConfigServiceFromEnv(const ELogPropertySequence* props, const ELogConfigMapNode* cfgMap,
                              std::string& configServicePublisher) {
    // update from environment only once, so that we can override environemnt settings through
    // manual/periodic updates
    if (sUpdatedFromEnv.load(std::memory_order_relaxed)) {
        return true;
    }

    ELogConfigServiceParams& params = modifyParams().m_configServiceParams;
    if (!getBoolEnv(ELOG_ENABLE_CONFIG_SERVICE_NAME, params.m_enableConfigService)) {
        return false;
    }

    // get host/port
    getStringEnv(ELOG_CONFIG_SERVICE_INTERFACE_NAME, params.m_configServiceHost);
    if (!getIntEnv(ELOG_CONFIG_SERVICE_PORT_NAME, params.m_configServicePort)) {
        return false;
    }

    // now check for publisher
    if (!getBoolEnv(ELOG_ENABLE_CONFIG_SERVICE_PUBLISHER_NAME, params.m_enablePublisher)) {
        return false;
    }
    getStringEnv(ELOG_CONFIG_SERVICE_PUBLISHER_NAME, configServicePublisher);
    return true;
}

bool loadConfigServicePublisher(const char* configServicePublisher,
                                const ELogPropertySequence* props,
                                const ELogConfigMapNode* cfgMap) {
    ELogConfigServicePublisher* publisher = constructConfigServicePublisher(configServicePublisher);
    if (publisher == nullptr) {
        ELOG_REPORT_ERROR("Failed to load remote configuration service publisher by name: %s",
                          configServicePublisher);
        return false;
    }

    // load publisher from props/cfg
    bool res = (props != nullptr) ? publisher->load(*props) : publisher->load(cfgMap);
    if (!res) {
        ELOG_REPORT_ERROR("Failed to load remote configuration service publisher");
        delete publisher;
        return false;
    }
    if (!publisher->initialize()) {
        ELOG_REPORT_ERROR("Failed to initialize remote configuration service publisher");
        delete publisher;
        return false;
    }
    updatePublisher(publisher, true);
    return true;
}

bool enableConfigService() {
    modifyParams().m_configServiceParams.m_enableConfigService = true;
    return true;
}

bool disableConfigService() {
    if (getParams().m_configServiceParams.m_enableConfigService) {
        if (!stopConfigService()) {
            ELOG_REPORT_ERROR("Failed to stop the remote configuration service due to disable");
            return false;
        }
        modifyParams().m_configServiceParams.m_enableConfigService = false;
    }
    return true;
}

bool startConfigService() {
    if (!getParams().m_configServiceParams.m_enableConfigService) {
        ELOG_REPORT_ERROR("Cannot start the remote configuration service, it is disabled");
        return false;
    }

    // take up to date parameters
    ELogConfigService* configService = ELogConfigService::getInstance();
    const ELogConfigServiceParams& params = getParams().m_configServiceParams;
    configService->setListenAddress(params.m_configServiceHost.c_str(), params.m_configServicePort);
    if (!params.m_enablePublisher) {
        if (params.m_publisher != nullptr) {
            ELOG_REPORT_NOTICE("Configuration service publisher not used since it is disabled");
        }
        configService->setPublisher(nullptr);
    } else {
        configService->setPublisher(params.m_publisher);
    }

    // start the service
    commutil::ErrorCode rc = configService->start();
    if (rc != commutil::ErrorCode::E_OK && rc != commutil::ErrorCode::E_INVALID_STATE) {
        ELOG_REPORT_ERROR("Failed to start the remote configuration service: %s",
                          commutil::errorCodeToString(rc));
        return false;
    }
    return true;
}

bool stopConfigService() {
    if (!getParams().m_configServiceParams.m_enableConfigService) {
        ELOG_REPORT_ERROR("Cannot stop the remote configuration service, it is disabled");
        return false;
    }
    commutil::ErrorCode rc = ELogConfigService::getInstance()->stop();
    if (rc != commutil::ErrorCode::E_OK && rc != commutil::ErrorCode::E_INVALID_STATE) {
        ELOG_REPORT_ERROR("Failed to stop the remote configuration service: %s",
                          commutil::errorCodeToString(rc));
        return false;
    }
    return true;
}

bool restartConfigService() {
    if (!stopConfigService()) {
        return false;
    }

    // NOTE: call to start takes up to date parameters
    return startConfigService();
}

bool setConfigServiceDetails(const char* host, int port, bool restartConfigService /* = false */) {
    if (restartConfigService) {
        if (!stopConfigService()) {
            return false;
        }
    }

    modifyParams().m_configServiceParams.m_configServiceHost = host;
    modifyParams().m_configServiceParams.m_configServicePort = port;
    ELogConfigService::getInstance()->setListenAddress(host, port);

    if (restartConfigService) {
        if (!startConfigService()) {
            return false;
        }
    }
    return true;
}

bool enableConfigServicePublisher(bool restartConfigService /* = false */) {
    if (getParams().m_configServiceParams.m_enablePublisher) {
        // silently ignore request
    }
    if (restartConfigService) {
        if (!stopConfigService()) {
            return false;
        }
    }
    modifyParams().m_configServiceParams.m_enablePublisher = true;

    if (restartConfigService) {
        if (!startConfigService()) {
            return false;
        }
    }
    return true;
}

bool disableConfigServicePublisher(bool restartConfigService /* = false */) {
    if (!getParams().m_configServiceParams.m_enablePublisher) {
        // silently ignore request
    }
    if (restartConfigService) {
        if (!stopConfigService()) {
            return false;
        }
    }
    modifyParams().m_configServiceParams.m_enablePublisher = false;

    if (restartConfigService) {
        if (!startConfigService()) {
            return false;
        }
    }
    return true;
}

bool setConfigServicePublisher(ELogConfigServicePublisher* publisher,
                               bool restartConfigService /* = false */) {
    if (restartConfigService) {
        if (!stopConfigService()) {
            return false;
        }
    }

    // NOTE: this publisher is managed by the caller, and
    // ELogConfigServicePublisher::initialize() must have already been called
    updatePublisher(publisher, false);
    ELogConfigService::getInstance()->setPublisher(publisher);

    if (restartConfigService) {
        if (!startConfigService()) {
            return false;
        }
    }
    return true;
}

void updatePublisher(ELogConfigServicePublisher* publisher, bool isInternalPublisher = true) {
    ELogConfigServiceParams& configServiceParams = modifyParams().m_configServiceParams;
    if (configServiceParams.m_publisher != nullptr) {
        if (sShouldDeletePublisher.load(std::memory_order_relaxed)) {
            if (!configServiceParams.m_publisher->terminate()) {
                ELOG_REPORT_ERROR(
                    "Failed to terminate %s configuration service publisher, undefined behavior "
                    "may be observed",
                    configServiceParams.m_publisher->getName());
            }
            delete configServiceParams.m_publisher;
            sShouldDeletePublisher.store(false, std::memory_order_relaxed);
        }
        configServiceParams.m_publisher = nullptr;
    }
    configServiceParams.m_publisher = publisher;
    if (publisher != nullptr && isInternalPublisher) {
        sShouldDeletePublisher.store(true, std::memory_order_relaxed);
    }
}

}  // namespace elog

#endif  // ELOG_ENABLE_CONFIG_SERVICE