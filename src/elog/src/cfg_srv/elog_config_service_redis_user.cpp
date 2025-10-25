#include "cfg_srv/elog_config_service_redis_user.h"

#ifdef ELOG_ENABLE_CONFIG_PUBLISH_REDIS

#include <cinttypes>
#include <cstring>

#include "elog_field_selector_internal.h"
#include "elog_report.h"
#include "elog_time.h"

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogConfigServiceRedisUser)

int convertVerifyMode(ELogRedisSslVerifyMode verifyMode) {
    switch (verifyMode) {
        case ELogRedisSslVerifyMode::VM_NONE:
            return REDIS_SSL_VERIFY_NONE;
        case ELogRedisSslVerifyMode::VM_PEER:
            return REDIS_SSL_VERIFY_PEER;
        case ELogRedisSslVerifyMode::VM_FAIL_IF_NO_PEER_CERT:
            return REDIS_SSL_VERIFY_FAIL_IF_NO_PEER_CERT;
        case ELogRedisSslVerifyMode::VM_CLIENT_ONCE:
            return REDIS_SSL_VERIFY_CLIENT_ONCE;
        case ELogRedisSslVerifyMode::VM_POST_HANDSHAKE:
            return REDIS_SSL_VERIFY_POST_HANDSHAKE;
        default:
            return -1;
    }
}

void ELogConfigServiceRedisUser::setSSLOptions(const char* caCertFileName, const char* caPath,
                                               const char* certFileName,
                                               const char* privateKeyFileName,
                                               const char* serverName,
                                               ELogRedisSslVerifyMode verifyMode) {
    m_params.m_usingSSL = true;
    m_params.m_caCertFileName = caCertFileName;
    m_params.m_caPath = caPath;
    m_params.m_certFileName = certFileName;
    m_params.m_privateKeyFileName = privateKeyFileName;
    m_params.m_serverName = serverName;
    m_params.m_verifyMode = verifyMode;
}

bool ELogConfigServiceRedisUser::initializeRedis() {
    // verify user provided all details
    if (m_params.m_serverList.empty()) {
        ELOG_REPORT_ERROR("Cannot start redis configuration service user: no redis server defined");
        return false;
    }
    if (m_params.m_key.empty()) {
        ELOG_REPORT_ERROR("Cannot start redis configuration service user: no publish key defined");
        return false;
    }

    // configure redis client
    for (const auto& server : m_params.m_serverList) {
        m_redisClient.addServer(server.getHost(), server.getPort());
    }
    m_redisClient.setPassword(m_params.m_password.c_str());
    if (m_params.m_usingSSL) {
        redisSSLOptions opts;
        opts.cacert_filename = m_params.m_caCertFileName.c_str();
        opts.capath = m_params.m_caPath.c_str();
        opts.cert_filename = m_params.m_certFileName.c_str();
        opts.private_key_filename = m_params.m_privateKeyFileName.c_str();
        opts.server_name = m_params.m_serverName.c_str();
        opts.verify_mode = convertVerifyMode(m_params.m_verifyMode);
        if (opts.verify_mode == -1) {
            ELOG_REPORT_ERROR("Invalid SSL verification mode");
            return false;
        }
        m_redisClient.setSSL(opts);
    }

    // we connect on-demand
    return true;
}

bool ELogConfigServiceRedisUser::terminateRedis() {
    if (m_redisClient.isRedisConnected()) {
        m_redisClient.disconnectRedis();
    }
    return true;
}

bool ELogConfigServiceRedisUser::isRedisConnected() { return m_redisClient.isRedisConnected(); }

bool ELogConfigServiceRedisUser::connectRedis() { return m_redisClient.connectRedis(); }

}  // namespace elog

#endif  // ELOG_ENABLE_CONFIG_PUBLISH_REDIS