#include "msg/elog_msg_config_loader.h"

#ifdef ELOG_ENABLE_MSG

#include <cinttypes>

#include "elog_config_loader.h"
#include "elog_report.h"

// TODO: this is almost an exact duplicate of http config, can we change it into common
// msg/transport config?

namespace elog {

bool ELogMsgConfigLoader::loadMsgConfig(const ELogConfigMapNode* logTargetCfg,
                                        const char* targetName, commutil::MsgConfig& msgConfig) {
    if (!ELogConfigLoader::getOptionalLogTargetTimeoutProperty(
            logTargetCfg, targetName, "connect_timeout", msgConfig.m_connectTimeoutMillis,
            ELogTimeUnits::TU_MILLI_SECONDS)) {
        return false;
    }
    if (!ELogConfigLoader::getOptionalLogTargetTimeoutProperty(
            logTargetCfg, targetName, "send_timeout", msgConfig.m_sendTimeoutMillis,
            ELogTimeUnits::TU_MILLI_SECONDS)) {
        return false;
    }
    if (!ELogConfigLoader::getOptionalLogTargetTimeoutProperty(
            logTargetCfg, targetName, "resend_timeout", msgConfig.m_resendPeriodMillis,
            ELogTimeUnits::TU_MILLI_SECONDS)) {
        return false;
    }
    if (!ELogConfigLoader::getOptionalLogTargetTimeoutProperty(
            logTargetCfg, targetName, "expire_timeout", msgConfig.m_expireTimeoutMillis,
            ELogTimeUnits::TU_MILLI_SECONDS)) {
        return false;
    }
    if (!ELogConfigLoader::getOptionalLogTargetSizeProperty(
            logTargetCfg, targetName, "backlog_limit", msgConfig.m_backlogLimitBytes,
            ELogSizeUnits::SU_BYTES)) {
        return false;
    }
    if (!ELogConfigLoader::getOptionalLogTargetTimeoutProperty(
            logTargetCfg, targetName, "shutdown_timeout", msgConfig.m_shutdownTimeoutMillis,
            ELogTimeUnits::TU_MILLI_SECONDS)) {
        return false;
    }
    if (!ELogConfigLoader::getOptionalLogTargetTimeoutProperty(
            logTargetCfg, targetName, "shutdown_polling_timeout",
            msgConfig.m_shutdownPollingTimeoutMillis, ELogTimeUnits::TU_MILLI_SECONDS)) {
        return false;
    }

    // TODO: impose valid ranges and issue warnings if out of range
    return true;
}

}  // namespace elog

#endif  // ELOG_ENABLE_HTTP