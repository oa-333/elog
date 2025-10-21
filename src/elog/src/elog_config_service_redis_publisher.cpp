#include "elog_config_service_redis_publisher.h"

#ifdef ELOG_ENABLE_CONFIG_PUBLISH_REDIS

#include <cinttypes>

#include "elog_common.h"
#include "elog_config_parser.h"
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

static bool loadCfg(const ELogConfigMapNode* cfg, const char* propName, std::string& value,
                    bool isMandatory) {
    bool found = false;
    if (!cfg->getStringValue(propName, found, value)) {
        ELOG_REPORT_ERROR(
            "Failed to load redis configuration service publisher, error in property %s", propName);
        return false;
    }
    if (!found) {
        if (isMandatory) {
            ELOG_REPORT_ERROR("Missing property %s for redis configuration service publisher",
                              propName);
            return false;
        } else if (!value.empty()) {
            ELOG_REPORT_NOTICE(
                "Missing property %s for redis configuration service publisher, default value will "
                "be used: %s",
                propName, value.c_str());
        }
    }
    return true;
}

static bool loadIntCfg(const ELogConfigMapNode* cfg, const char* propName, uint32_t& value,
                       bool isMandatory) {
    bool found = false;
    int64_t value64 = 0;
    if (!cfg->getIntValue(propName, found, value64)) {
        ELOG_REPORT_ERROR(
            "Failed to load redis configuration service publisher, error in property %s", propName);
        return false;
    }
    if (!found) {
        if (isMandatory) {
            ELOG_REPORT_ERROR("Missing property %s for redis configuration service publisher",
                              propName);
            return false;
        } else {
            ELOG_REPORT_NOTICE(
                "Missing property %s for redis configuration service publisher, default value will "
                "be used: %u",
                propName, value);
        }
    } else if (value64 > UINT_MAX || value64 < 0) {
        ELOG_REPORT_ERROR(
            "Property %s for redis configuration service publisher out of range [0, %u]: %" PRId64,
            UINT_MAX, value64);
        return false;
    }
    return true;
}

static bool loadProp(const ELogPropertySequence& props, const char* propName, std::string& value,
                     bool isMandatory) {
    if (!getProp(props, propName, value)) {
        if (isMandatory) {
            ELOG_REPORT_ERROR("Missing property %s for redis configuration service publisher",
                              propName);
            return false;
        } else if (!value.empty()) {
            ELOG_REPORT_NOTICE(
                "Missing property %s for redis configuration service publisher, default value will "
                "be used: %s",
                propName, value.c_str());
        }
    }
    return true;
}

static bool loadIntProp(const ELogPropertySequence& props, const char* propName, uint32_t& value,
                        bool isMandatory) {
    bool found = false;
    if (!getIntProp(props, propName, value, &found)) {
        return false;
    }
    if (isMandatory && !found) {
        ELOG_REPORT_ERROR("Missing property %s for redis configuration service publisher",
                          propName);
        return false;
    } else if (!found) {
        ELOG_REPORT_NOTICE(
            "Missing property %s for redis configuration service publisher, default value will "
            "be used: %u",
            propName, value);
    }
    return true;
}

static bool loadEnvCfg(const ELogConfigMapNode* cfg, const char* propName, std::string& value,
                       bool mandatory) {
    if (!getStringEnv(propName, value)) {
        if (!loadCfg(cfg, propName, value, mandatory)) {
            return false;
        }
    }
    return true;
}

static bool loadIntEnvCfg(const ELogConfigMapNode* cfg, const char* propName, uint32_t& value,
                          bool mandatory) {
    bool found = false;
    if (!getIntEnv(propName, value, true, &found)) {
        return false;
    }
    if (!found) {
        if (!loadIntCfg(cfg, propName, value, mandatory)) {
            return false;
        }
    }
    return true;
}

static bool loadEnvProps(const ELogPropertySequence& props, const char* propName,
                         std::string& value, bool mandatory) {
    if (!getStringEnv(propName, value)) {
        if (!loadProp(props, propName, value, mandatory)) {
            return false;
        }
    }
    return true;
}

static bool loadIntEnvProps(const ELogPropertySequence& props, const char* propName,
                            uint32_t& value, bool mandatory) {
    bool found = false;
    if (!getIntEnv(propName, value, true, &found)) {
        return false;
    }
    if (!found) {
        if (!loadIntProp(props, propName, value, mandatory)) {
            return false;
        }
    }
    return true;
}

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

void ELogConfigServiceRedisPublisher::setSSLOptions(const char* caCertFileName, const char* caPath,
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
    if (!loadEnvProps(props, ELOG_CFG_REDIS_SERVERS, serverListStr, true)) {
        return false;
    }
    if (!parseServerListString(serverListStr)) {
        return false;
    }

    // get optional key
    if (!loadEnvProps(props, ELOG_CFG_REDIS_KEY, params.m_key, false)) {
        return false;
    }

    // redis key expiry in seconds
    if (!loadIntEnvProps(props, ELOG_CFG_REDIS_EXPIRE_SECONDS, params.m_expirySeconds, false)) {
        return false;
    }

    // redis renew expiry in seconds
    if (!loadIntEnvProps(props, ELOG_CFG_REDIS_RENEW_EXPIRE_SECONDS,
                         params.m_renewExpiryTimeoutSeconds, false)) {
        return false;
    }

    // get optional password
    if (!loadEnvProps(props, ELOG_CFG_REDIS_PASSWORD, params.m_password, false)) {
        return false;
    }

    // load SSL configuration
    if (!loadEnvProps(props, ELOG_CFG_REDIS_CA_CERT_FILE, params.m_caCertFileName, false)) {
        return false;
    }
    if (!loadEnvProps(props, ELOG_CFG_REDIS_CA_PATH, params.m_caPath, false)) {
        return false;
    }
    if (!loadEnvProps(props, ELOG_CFG_REDIS_CERT_FILE, params.m_certFileName, false)) {
        return false;
    }
    if (!loadEnvProps(props, ELOG_CFG_REDIS_PRIVATE_KEY_FILE, params.m_privateKeyFileName, false)) {
        return false;
    }
    if (!loadEnvProps(props, ELOG_CFG_REDIS_SERVER_NAME, params.m_serverName, false)) {
        return false;
    }
    std::string verifyMode;
    if (!loadEnvProps(props, ELOG_CFG_REDIS_VERIFY_MODE, verifyMode, false)) {
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

bool ELogConfigServiceRedisPublisher::initialize() {
    // verify user provided all details
    if (m_params.m_serverList.empty()) {
        ELOG_REPORT_ERROR(
            "Cannot start redis configuration service publisher: no redis server defined");
        return false;
    }
    if (m_params.m_key.empty()) {
        ELOG_REPORT_ERROR(
            "Cannot start redis configuration service publisher: no publish key defined");
        return false;
    }

    // configure redis client
    m_redisClient.setServerList(m_params.m_serverList);
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

bool ELogConfigServiceRedisPublisher::terminate() {
    if (m_redisClient.isRedisConnected()) {
        m_redisClient.disconnectRedis();
    }
    return true;
}

void ELogConfigServiceRedisPublisher::onConfigServiceStart(const char* host, int port) {
    // prepare unique key
    m_serviceSpec = m_params.m_key + ":" + host + ":" + std::to_string(port);

    // start the publish thread
    m_stopPublish = false;
    m_requiresPublish = true;
    m_publishThread = std::thread(&ELogConfigServiceRedisPublisher::publishThread, this);
}

void ELogConfigServiceRedisPublisher::onConfigServiceStop(const char* host, int port) {
    // stop publish thread
    {
        std::unique_lock<std::mutex> lock(m_lock);
        m_stopPublish = true;
        m_cv.notify_one();
    }
    m_publishThread.join();
}

bool ELogConfigServiceRedisPublisher::parseServerListString(const std::string& serverListStr) {
    std::vector<std::string> serverList;
    tokenize(serverListStr.c_str(), serverList, ";,");
    for (const std::string& server : serverList) {
        std::string host;
        int port = 0;
        if (!ELogConfigParser::parseHostPort(server, host, port)) {
            ELOG_REPORT_ERROR("Invalid redis server specification, cannot parse host and port: %s",
                              server.c_str());
            return false;
        }
        m_params.m_serverList.push_back({host, port});
    }
    return true;
}

void ELogConfigServiceRedisPublisher::publishThread() {
    std::unique_lock<std::mutex> lock(m_lock);
    while (!m_stopPublish) {
        m_cv.wait_for(lock, std::chrono::seconds(m_params.m_renewExpiryTimeoutSeconds),
                      [this]() { return m_stopPublish; });
        if (!m_stopPublish) {
            // NOTE: we must release lock first, since if any of the following call blocks, we end
            // up in deadlock, since call to stop publish will get stuck on join publish thread
            lock.unlock();

            // reconnect if needed, write key or renew expiry
            execPublishService();

            // NOTE: must reacquire lock before next access to condition variable
            lock.lock();
        }
    }

    // last attempt to remove entry from redis
    // NOTE: unlock first to avoid any deadlocks
    lock.unlock();
    if (m_redisClient.isRedisConnected()) {
        unpublishConfigService();
    }
}

void ELogConfigServiceRedisPublisher::execPublishService() {
    // if not connected then reconnect first
    if (!m_redisClient.isRedisConnected()) {
        if (!m_redisClient.connectRedis()) {
            return;
        }
        ELOG_REPORT_INFO("Configuration service publisher was able to connect to Redis server");
        m_requiresPublish = true;
    }

    // publish if required, otherwise renew expiry of publish key
    if (m_requiresPublish) {
        publishConfigService();
    } else {
        renewExpiry();

        // don't wait for next round, publish now
        if (m_requiresPublish) {
            publishConfigService();
        }
    }
}

void ELogConfigServiceRedisPublisher::publishConfigService() {
    // try to set the key with expiry
    const char* appName = getAppName();
    if (appName == nullptr || *appName == 0) {
        appName = getProgramName();
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
    } else {
        m_requiresPublish = false;
    }
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
            m_requiresPublish = true;
        }
        return reply;
    });

    if (!res) {
        ELOG_REPORT_ERROR("Failed to renew expiry of redis key %s", m_serviceSpec.c_str());
    }
}

}  // namespace elog

#endif  // ELOG_ENABLE_CONFIG_PUBLISH_REDIS