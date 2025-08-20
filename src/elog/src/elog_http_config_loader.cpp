#include "elog_http_config_loader.h"

#ifdef ELOG_ENABLE_HTTP

#include <cinttypes>

#include "elog_config_loader.h"
#include "elog_report.h"

namespace elog {

bool ELogHttpConfigLoader::loadHttpConfig(const ELogConfigMapNode* logTargetCfg,
                                          const char* targetName, ELogHttpConfig& httpConfig) {
    if (!ELogConfigLoader::getOptionalLogTargetTimeoutProperty(
            logTargetCfg, targetName, "connect_timeout", httpConfig.m_connectTimeoutMillis,
            ELogTimeUnits::TU_MILLI_SECONDS)) {
        return false;
    }
    if (!ELogConfigLoader::getOptionalLogTargetTimeoutProperty(
            logTargetCfg, targetName, "write_timeout", httpConfig.m_writeTimeoutMillis,
            ELogTimeUnits::TU_MILLI_SECONDS)) {
        return false;
    }
    if (!ELogConfigLoader::getOptionalLogTargetTimeoutProperty(
            logTargetCfg, targetName, "read_timeout", httpConfig.m_readTimeoutMillis,
            ELogTimeUnits::TU_MILLI_SECONDS)) {
        return false;
    }
    if (!ELogConfigLoader::getOptionalLogTargetTimeoutProperty(
            logTargetCfg, targetName, "resend_timeout", httpConfig.m_resendPeriodMillis,
            ELogTimeUnits::TU_MILLI_SECONDS)) {
        return false;
    }
    if (!ELogConfigLoader::getOptionalLogTargetSizeProperty(
            logTargetCfg, targetName, "backlog_limit", httpConfig.m_backlogLimitBytes,
            ELogSizeUnits::SU_BYTES)) {
        return false;
    }
    if (!ELogConfigLoader::getOptionalLogTargetTimeoutProperty(
            logTargetCfg, targetName, "shutdown_timeout", httpConfig.m_shutdownTimeoutMillis,
            ELogTimeUnits::TU_MILLI_SECONDS)) {
        return false;
    }
    return true;
}

}  // namespace elog

#endif  // ELOG_ENABLE_HTTP