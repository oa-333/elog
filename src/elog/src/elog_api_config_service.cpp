#include "elog_api_config_service.h"

#ifdef ELOG_ENABLE_CONFIG_SERVICE

#include "elog_config_service.h"
#include "elog_internal.h"
#include "elog_report.h"

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogConfigServiceApi)

bool initConfigService() {
    if (!ELogConfigService::createInstance()) {
        return false;
    }
    ELogConfigService* configService = ELogConfigService::getInstance();
    if (configService->initialize(getParams().m_hostInterface.c_str(), getParams().m_port,
                                  getParams().m_publisher) != commutil::ErrorCode::E_OK) {
        ELogConfigService::destroyInstance();
        return false;
    }
    if (configService->start() != commutil::ErrorCode::E_OK) {
        configService->terminate();
        ELogConfigService::destroyInstance();
        return false;
    }
    return true;
}

void termConfigService() {
    ELogConfigService* configService = ELogConfigService::getInstance();
    if (configService != nullptr) {
        configService->stop();
        configService->terminate();
        ELogConfigService::destroyInstance();
    }
}

bool configConfigServiceProps(const ELogPropertySequence& props) {
    std::string configServiceInterface;
    if (getProp(props, ELOG_CONFIG_SERVICE_INTERFACE_NAME, configServiceInterface)) {
        int configServicePort = 0;
        std::string configServicePortStr;
        if (getProp(props, ELOG_CONFIG_SERVICE_PORT_NAME, configServicePortStr)) {
            if (!parseIntProp(ELOG_CONFIG_SERVICE_PORT_NAME, "", configServicePortStr,
                              configServicePort)) {
                ELOG_REPORT_ERROR("Invalid configuration service port: %s",
                                  configServicePortStr.c_str());
                return false;
            }
        }
        commutil::ErrorCode rc = ELogConfigService::getInstance()->restart(
            configServiceInterface.c_str(), configServicePort);
        if (rc != commutil::ErrorCode::E_OK) {
            ELOG_REPORT_ERROR("Failed to restart the configuration service on %s:%d: %s",
                              configServiceInterface.c_str(), configServicePort,
                              commutil::errorCodeToString(rc));
            return false;
        }
    }
    return true;
}

bool configConfigService(const ELogConfigMapNode* cfgMap) {
    const ELogConfigValue* configServiceInterfaceValue =
        cfgMap->getValue(ELOG_CONFIG_SERVICE_INTERFACE_NAME);
    const ELogConfigValue* configServicePortValue = cfgMap->getValue(ELOG_CONFIG_SERVICE_PORT_NAME);
    if (configServiceInterfaceValue != nullptr || configServicePortValue != nullptr) {
        std::string configServiceInterface;
        int configServicePort = 0;
        if (configServiceInterfaceValue != nullptr) {
            if (configServiceInterfaceValue->getValueType() !=
                ELogConfigValueType::ELOG_CONFIG_STRING_VALUE) {
                ELOG_REPORT_ERROR("Invalid type for %s, expecting string",
                                  ELOG_CONFIG_SERVICE_INTERFACE_NAME);
                return false;
            }
            configServiceInterface =
                ((const ELogConfigStringValue*)configServiceInterfaceValue)->getStringValue();
        }
        if (configServicePortValue != nullptr) {
            if (configServicePortValue->getValueType() !=
                ELogConfigValueType::ELOG_CONFIG_INT_VALUE) {
                ELOG_REPORT_ERROR("Invalid type for %s, expecting integer",
                                  ELOG_CONFIG_SERVICE_PORT_NAME);
                return false;
            }
            int64_t intValue = ((const ELogConfigIntValue*)configServicePortValue)->getIntValue();
            if (intValue < 0 || intValue > INT_MAX) {
                ELOG_REPORT_ERROR("Invalid port value %" PRId64
                                  " specified for %s, out of valid range [0, %d]",
                                  intValue, ELOG_CONFIG_SERVICE_PORT_NAME, (int)INT_MAX);
                return false;
            }
            configServicePort = (int)intValue;
        }

        commutil::ErrorCode rc = ELogConfigService::getInstance()->restart(
            configServiceInterface.c_str(), configServicePort);
        if (rc != commutil::ErrorCode::E_OK) {
            ELOG_REPORT_ERROR("Failed to restart the configuration service on %s:%d: %s",
                              configServiceInterface.c_str(), configServicePort,
                              commutil::errorCodeToString(rc));
            return false;
        }
    }
    return true;
}

}  // namespace elog

#endif  // ELOG_ENABLE_CONFIG_SERVICE