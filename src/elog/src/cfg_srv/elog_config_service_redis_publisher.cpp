#include "cfg_srv/elog_config_service_redis_publisher.h"

#ifdef ELOG_ENABLE_CONFIG_PUBLISH_REDIS

#include <cinttypes>
#include <cstring>

#include "elog_field_selector_internal.h"
#include "elog_report.h"
#include "elog_time.h"

#define ELOG_CFG_REDIS_SERVERS "redis_servers"
#define ELOG_CFG_REDIS_KEY "redis_key"
#define ELOG_CFG_REDIS_PASSWORD "redis_password"
#define ELOG_CFG_REDIS_CA_CERT_FILE "redis_ca_cert_file"
#define ELOG_CFG_REDIS_CA_PATH "redis_ca_path"
#define ELOG_CFG_REDIS_CERT_FILE "redis_cert_file"
#define ELOG_CFG_REDIS_PRIVATE_KEY_FILE "redis_private_key_file"
#define ELOG_CFG_REDIS_SERVER_NAME "redis_server_name"
#define ELOG_CFG_REDIS_VERIFY_MODE "redis_verify_mode"
#define ELOG_CFG_REDIS_EXPIRE_SECONDS "redis_expire_seconds"
#define ELOG_CFG_REDIS_RENEW_EXPIRE_SECONDS "redis_renew_expire_seconds"

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogConfigServiceRedisPublisher)

ELOG_IMPLEMENT_CONFIG_SERVICE_PUBLISHER(ELogConfigServiceRedisPublisher)

static bool verifyModeFromString(const char* verifyModeStr, ELogRedisSslVerifyMode& verifyMode) {
    if (strcmp(verifyModeStr, "none") == 0) {
        verifyMode = ELogRedisSslVerifyMode::VM_NONE;
    } else if (strcmp(verifyModeStr, "peer") == 0) {
        verifyMode = ELogRedisSslVerifyMode::VM_PEER;
    } else if (strcmp(verifyModeStr, "fail_no_peer_cert") == 0) {
        verifyMode = ELogRedisSslVerifyMode::VM_FAIL_IF_NO_PEER_CERT;
    } else if (strcmp(verifyModeStr, "client_once") == 0) {
        verifyMode = ELogRedisSslVerifyMode::VM_CLIENT_ONCE;
    } else if (strcmp(verifyModeStr, "post_handshake") == 0) {
        verifyMode = ELogRedisSslVerifyMode::VM_POST_HANDSHAKE;
    } else {
        ELOG_REPORT_ERROR("Invalid Redis SSL verify mode: %s", verifyModeStr);
        return false;
    }
    return true;
}

ELogConfigServiceRedisPublisher* ELogConfigServiceRedisPublisher::create() {
    return new (std::nothrow) ELogConfigServiceRedisPublisher();
}

void ELogConfigServiceRedisPublisher::destroy(ELogConfigServiceRedisPublisher* publisher) {
    if (publisher != nullptr) {
        delete publisher;
    }
}

bool ELogConfigServiceRedisPublisher::load(const ELogConfigMapNode* cfg) {
    // load everything into parameters object
    ELogConfigServiceRedisParams params;

    // NOTE: we allow override from environment variables

    // get mandatory server list
    std::string serverListStr;
    if (!loadEnvCfg(cfg, ELOG_CFG_REDIS_SERVERS, serverListStr, true)) {
        return false;
    }
    if (!parseServerListString(serverListStr)) {
        return false;
    }

    // get optional key
    if (!loadEnvCfg(cfg, ELOG_CFG_REDIS_KEY, params.m_key, false)) {
        return false;
    }

    // redis key expiry in seconds
    if (!loadIntEnvCfg(cfg, ELOG_CFG_REDIS_EXPIRE_SECONDS, params.m_expirySeconds, false)) {
        return false;
    }

    // redis renew expiry in seconds
    if (!loadIntEnvCfg(cfg, ELOG_CFG_REDIS_RENEW_EXPIRE_SECONDS, params.m_renewExpiryTimeoutSeconds,
                       false)) {
        return false;
    }

    // get optional password
    if (!loadEnvCfg(cfg, ELOG_CFG_REDIS_PASSWORD, params.m_password, false)) {
        return false;
    }

    // load SSL configuration
    if (!loadEnvCfg(cfg, ELOG_CFG_REDIS_CA_CERT_FILE, params.m_caCertFileName, false)) {
        return false;
    }
    if (!loadEnvCfg(cfg, ELOG_CFG_REDIS_CA_PATH, params.m_caPath, false)) {
        return false;
    }
    if (!loadEnvCfg(cfg, ELOG_CFG_REDIS_CERT_FILE, params.m_certFileName, false)) {
        return false;
    }
    if (!loadEnvCfg(cfg, ELOG_CFG_REDIS_PRIVATE_KEY_FILE, params.m_privateKeyFileName, false)) {
        return false;
    }
    if (!loadEnvCfg(cfg, ELOG_CFG_REDIS_SERVER_NAME, params.m_serverName, false)) {
        return false;
    }
    std::string verifyMode;
    if (!loadEnvCfg(cfg, ELOG_CFG_REDIS_VERIFY_MODE, verifyMode, false)) {
        return false;
    }
    if (!verifyMode.empty()) {
        if (!verifyModeFromString(verifyMode.c_str(), params.m_verifyMode)) {
            return false;
        }
    }

    // configure and return
    configure(params);
    return true;
}

bool ELogConfigServiceRedisPublisher::load(const ELogPropertySequence& props) {
    // load everything into parameters object
    ELogConfigServiceRedisParams params;

    // NOTE: we allow override from environment variables

    // get mandatory server list
    std::string serverListStr;
    if (!loadEnvProp(props, ELOG_CFG_REDIS_SERVERS, serverListStr, true)) {
        return false;
    }
    if (!parseServerListString(serverListStr)) {
        return false;
    }

    // get optional key
    if (!loadEnvProp(props, ELOG_CFG_REDIS_KEY, params.m_key, false)) {
        return false;
    }

    // redis key expiry in seconds
    if (!loadIntEnvProp(props, ELOG_CFG_REDIS_EXPIRE_SECONDS, params.m_expirySeconds, false)) {
        return false;
    }

    // redis renew expiry in seconds
    if (!loadIntEnvProp(props, ELOG_CFG_REDIS_RENEW_EXPIRE_SECONDS,
                        params.m_renewExpiryTimeoutSeconds, false)) {
        return false;
    }

    // get optional password
    if (!loadEnvProp(props, ELOG_CFG_REDIS_PASSWORD, params.m_password, false)) {
        return false;
    }

    // load SSL configuration
    if (!loadEnvProp(props, ELOG_CFG_REDIS_CA_CERT_FILE, params.m_caCertFileName, false)) {
        return false;
    }
    if (!loadEnvProp(props, ELOG_CFG_REDIS_CA_PATH, params.m_caPath, false)) {
        return false;
    }
    if (!loadEnvProp(props, ELOG_CFG_REDIS_CERT_FILE, params.m_certFileName, false)) {
        return false;
    }
    if (!loadEnvProp(props, ELOG_CFG_REDIS_PRIVATE_KEY_FILE, params.m_privateKeyFileName, false)) {
        return false;
    }
    if (!loadEnvProp(props, ELOG_CFG_REDIS_SERVER_NAME, params.m_serverName, false)) {
        return false;
    }
    std::string verifyMode;
    if (!loadEnvProp(props, ELOG_CFG_REDIS_VERIFY_MODE, verifyMode, false)) {
        return false;
    }
    if (!verifyMode.empty()) {
        if (!verifyModeFromString(verifyMode.c_str(), params.m_verifyMode)) {
            return false;
        }
    }

    // configure and return
    configure(params);
    return true;
}

bool ELogConfigServiceRedisPublisher::initialize() { return initializeRedis(); }

bool ELogConfigServiceRedisPublisher::terminate() { return terminateRedis(); }

void ELogConfigServiceRedisPublisher::onConfigServiceStart(const char* host, int port) {
    // prepare unique key
    m_serviceSpec = m_params.m_key + ":" + host + ":" + std::to_string(port);

    // start the publish thread
    startPublishThread(m_params.m_renewExpiryTimeoutSeconds);
}

void ELogConfigServiceRedisPublisher::onConfigServiceStop(const char* host, int port) {
    stopPublishThread();
}

bool ELogConfigServiceRedisPublisher::publishConfigService() {
    // try to set the key with expiry
    const char* appName = getAppNameField();
    if (appName == nullptr || *appName == 0) {
        appName = getProgramNameField();
    }
    std::string value = std::string(appName);
    bool res = m_redisClient.visitRedisCommand([this, &value](redisContext* conext) {
        ELOG_REPORT_TRACE("Executing redis command SET %s %s EX %d", m_serviceSpec.c_str(),
                          value.c_str(), m_params.m_expirySeconds);
        return redisCommand(conext, "SET %s %s EX %d", m_serviceSpec.c_str(), value.c_str(),
                            m_params.m_expirySeconds);
    });

    if (!res) {
        ELOG_REPORT_ERROR("Failed to add value %s to Redis set %s", value.c_str(),
                          m_params.m_key.c_str());
    }
    return res;
}

void ELogConfigServiceRedisPublisher::unpublishConfigService() {  // try to remove the key
    bool res = m_redisClient.visitRedisCommand([this](redisContext* conext) {
        return redisCommand(conext, "DEL %s", m_params.m_key.c_str());
    });

    if (!res) {
        ELOG_REPORT_ERROR("Failed to delete  Redis key %s", m_params.m_key.c_str());
    }
}

void ELogConfigServiceRedisPublisher::renewExpiry() {
    // try to set the key with expiry
    bool res = m_redisClient.visitRedisCommand([this](redisContext* conext) {
        redisReply* reply = (redisReply*)redisCommand(conext, "EXPIRE %s %d", m_serviceSpec.c_str(),
                                                      m_params.m_expirySeconds);
        if (!m_redisClient.checkReply(reply, REDIS_REPLY_INTEGER)) {
            ELOG_REPORT_ERROR("Failed to extend expiration of configuration service key %s",
                              m_serviceSpec.c_str());
        } else if (reply->integer == 0) {
            ELOG_REPORT_WARN(
                "Failed to extend expiration of configuration service key %s, key already expired",
                m_serviceSpec.c_str());
            // make sure the key will get published
            setRequiresPublish();
        }
        return reply;
    });

    if (!res) {
        ELOG_REPORT_ERROR("Failed to renew expiry of redis key %s", m_serviceSpec.c_str());
    }
}

bool ELogConfigServiceRedisPublisher::isConnected() { return m_redisClient.isRedisConnected(); }

bool ELogConfigServiceRedisPublisher::connect() { return m_redisClient.connectRedis(); }

}  // namespace elog

#endif  // ELOG_ENABLE_CONFIG_PUBLISH_REDIS