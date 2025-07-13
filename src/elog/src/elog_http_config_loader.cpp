#include "elog_http_config_loader.h"

#ifdef ELOG_ENABLE_HTTP

#include <cinttypes>

#include "elog_config_loader.h"
#include "elog_error.h"

namespace elog {

bool ELogHttpConfigLoader::loadHttpConfig(const ELogConfigMapNode* logTargetCfg,
                                          const char* targetName, ELogHttpConfig& httpConfig) {
    if (!loadHttpConfigValue(logTargetCfg, targetName, "connect_timeout_millis",
                             httpConfig.m_connectTimeoutMillis)) {
        return false;
    }
    if (!loadHttpConfigValue(logTargetCfg, targetName, "write_timeout_millis",
                             httpConfig.m_writeTimeoutMillis)) {
        return false;
    }
    if (!loadHttpConfigValue(logTargetCfg, targetName, "read_timeout_millis",
                             httpConfig.m_readTimeoutMillis)) {
        return false;
    }
    if (!loadHttpConfigValue(logTargetCfg, targetName, "resend_timeout_millis",
                             httpConfig.m_resendPeriodMillis)) {
        return false;
    }
    if (!loadHttpConfigValue(logTargetCfg, targetName, "backlog_limit_bytes",
                             httpConfig.m_backlogLimitBytes)) {
        return false;
    }
    if (!loadHttpConfigValue(logTargetCfg, targetName, "shutdown_timeout_millis",
                             httpConfig.m_shutdownTimeoutMillis)) {
        return false;
    }
    return true;
}

bool ELogHttpConfigLoader::loadHttpConfigValue(const ELogConfigMapNode* logTargetCfg,
                                               const char* targetName, const char* propName,
                                               uint32_t& value) {
    int64_t value64 = value;
    if (!ELogConfigLoader::getOptionalLogTargetIntProperty(logTargetCfg, targetName, propName,
                                                           value64)) {
        return false;
    }
    if (value64 >= UINT32_MAX) {
        ELOG_REPORT_ERROR("Property %s value %" PRId64
                          " in log target %s exceeds maximum allowed %u",
                          propName, value64, targetName, (unsigned)UINT32_MAX);
        return false;
    }
    value = (uint32_t)value64;
    return true;
}

}  // namespace elog

#endif  // ELOG_ENABLE_HTTP