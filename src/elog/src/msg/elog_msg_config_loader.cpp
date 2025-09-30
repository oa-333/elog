#include "msg/elog_msg_config_loader.h"

#ifdef ELOG_ENABLE_MSG

#include <cinttypes>

#include "elog_config_loader.h"
#include "elog_report.h"

// TODO: this is almost an exact duplicate of http config, can we change it into common
// msg/transport config?

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogMsgConfigLoader)

static bool loadTimeoutConfig(const ELogConfigMapNode* logTargetCfg, const char* targetName,
                              const char* propName, uint64_t& propValue, ELogTimeUnits targetUnits,
                              uint64_t minValue, uint64_t maxValue, uint64_t defaultValue) {
    if (!ELogConfigLoader::getOptionalLogTargetTimeoutProperty(logTargetCfg, targetName, propName,
                                                               propValue, targetUnits)) {
        return false;
    }
    if (!verifyUInt64PropRange(targetName, propName, propValue, minValue, maxValue, true,
                               defaultValue)) {
        return false;
    }
    return true;
}

bool ELogMsgConfigLoader::loadMsgConfig(const ELogConfigMapNode* logTargetCfg,
                                        const char* targetName, ELogMsgConfig& msgConfig) {
    std::string mode;
    bool found = false;
    if (!ELogConfigLoader::getOptionalLogTargetStringProperty(logTargetCfg, targetName, "mode",
                                                              mode, &found)) {
        return false;
    }

    msgConfig.m_syncMode = ELOG_MSG_DEFAULT_SYNC_MODE;
    if (found) {
        if (mode.compare("sync") == 0) {
            msgConfig.m_syncMode = true;
        } else if (mode.compare("async") != 0) {
            ELOG_REPORT_ERROR(
                "Invalid %s log target specification, unsupported property 'mode' value '%s' "
                "(context: %s)",
                targetName, mode.c_str(), logTargetCfg->getFullContext());
            return false;
        }
    }

    // compression flag
    msgConfig.m_compress = ELOG_MSG_DEFAULT_COMPRESS;
    if (!ELogConfigLoader::getOptionalLogTargetBoolProperty(logTargetCfg, targetName, "compress",
                                                            msgConfig.m_compress)) {
        return false;
    }

    // maximum concurrent requests
    msgConfig.m_maxConcurrentRequests = ELOG_MSG_DEFAULT_CONCURRENT_REQUESTS;
    if (!ELogConfigLoader::getOptionalLogTargetUInt32Property(logTargetCfg, targetName,
                                                              "max_concurrent_requests",
                                                              msgConfig.m_maxConcurrentRequests)) {
        return false;
    }
    // verify legal range for this value
    if (!verifyUInt32PropRange(targetName, "max_concurrent_requests",
                               msgConfig.m_maxConcurrentRequests, ELOG_MSG_MIN_CONCURRENT_REQUESTS,
                               ELOG_MSG_MAX_CONCURRENT_REQUESTS, true,
                               ELOG_MSG_DEFAULT_CONCURRENT_REQUESTS)) {
        return false;
    }

    // binary format
    std::string binaryFormat = ELOG_MSG_DEFAULT_BINARY_FORMAT;
    if (!ELogConfigLoader::getOptionalLogTargetStringProperty(logTargetCfg, targetName,
                                                              "binary_format", binaryFormat)) {
        return false;
    }
    msgConfig.m_binaryFormatProvider =
        constructBinaryFormatProvider(binaryFormat.c_str(), commutil::ByteOrder::NETWORK_ORDER);
    if (msgConfig.m_binaryFormatProvider == nullptr) {
        ELOG_REPORT_ERROR(
            "Invalid net log target specification, unsupported binary format '%s' (context: %s)",
            binaryFormat.c_str(), logTargetCfg->getFullContext());
        return false;
    }

    if (!loadTimeoutConfig(logTargetCfg, targetName, "connect_timeout",
                           msgConfig.m_commConfig.m_connectTimeoutMillis,
                           ELogTimeUnits::TU_MILLI_SECONDS, ELOG_MSG_MIN_CONNECT_TIMEOUT,
                           ELOG_MSG_MAX_CONNECT_TIMEOUT, ELOG_MSG_DEFAULT_CONNECT_TIMEOUT)) {
        return false;
    }

    if (!loadTimeoutConfig(logTargetCfg, targetName, "send_timeout",
                           msgConfig.m_commConfig.m_sendTimeoutMillis,
                           ELogTimeUnits::TU_MILLI_SECONDS, ELOG_MSG_MIN_SEND_TIMEOUT,
                           ELOG_MSG_MAX_SEND_TIMEOUT, ELOG_MSG_DEFAULT_SEND_TIMEOUT)) {
        return false;
    }

    if (!loadTimeoutConfig(logTargetCfg, targetName, "resend_timeout",
                           msgConfig.m_commConfig.m_resendPeriodMillis,
                           ELogTimeUnits::TU_MILLI_SECONDS, ELOG_MSG_MIN_RESEND_TIMEOUT,
                           ELOG_MSG_MAX_RESEND_TIMEOUT, ELOG_MSG_DEFAULT_RESEND_TIMEOUT)) {
        return false;
    }

    if (!loadTimeoutConfig(logTargetCfg, targetName, "expire_timeout",
                           msgConfig.m_commConfig.m_expireTimeoutMillis,
                           ELogTimeUnits::TU_MILLI_SECONDS, ELOG_MSG_MIN_EXPIRE_TIMEOUT,
                           ELOG_MSG_MAX_EXPIRE_TIMEOUT, ELOG_MSG_DEFAULT_EXPIRE_TIMEOUT)) {
        return false;
    }

    if (!ELogConfigLoader::getOptionalLogTargetSizeProperty(
            logTargetCfg, targetName, "backlog_limit", msgConfig.m_commConfig.m_backlogLimitBytes,
            ELogSizeUnits::SU_BYTES)) {
        return false;
    }
    if (!verifyUInt64PropRange(targetName, "backlog_limit",
                               msgConfig.m_commConfig.m_backlogLimitBytes,
                               ELOG_MSG_MIN_BACKLOG_SIZE, ELOG_MSG_MAX_BACKLOG_SIZE, true,
                               ELOG_MSG_DEFAULT_BACKLOG_SIZE)) {
        return false;
    }

    if (!loadTimeoutConfig(logTargetCfg, targetName, "shutdown_timeout",
                           msgConfig.m_commConfig.m_shutdownTimeoutMillis,
                           ELogTimeUnits::TU_MILLI_SECONDS, ELOG_MSG_MIN_SHUTDOWN_TIMEOUT,
                           ELOG_MSG_MAX_SHUTDOWN_TIMEOUT, ELOG_MSG_DEFAULT_SHUTDOWN_TIMEOUT)) {
        return false;
    }

    if (!loadTimeoutConfig(logTargetCfg, targetName, "shutdown_polling_timeout",
                           msgConfig.m_commConfig.m_shutdownTimeoutMillis,
                           ELogTimeUnits::TU_MILLI_SECONDS, ELOG_MSG_MIN_SHUTDOWN_POLLING_TIMEOUT,
                           ELOG_MSG_MAX_SHUTDOWN_POLLING_TIMEOUT,
                           ELOG_MSG_DEFAULT_SHUTDOWN_POLLING_TIMEOUT)) {
        return false;
    }

    return true;
}

}  // namespace elog

#endif  // ELOG_ENABLE_HTTP