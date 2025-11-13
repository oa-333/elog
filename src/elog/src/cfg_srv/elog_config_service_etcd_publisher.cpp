#include "cfg_srv/elog_config_service_etcd_publisher.h"

#ifdef ELOG_ENABLE_CONFIG_PUBLISH_ETCD

#include <cinttypes>
#include <nlohmann/json.hpp>

#include "elog_common.h"
#include "elog_field_selector_internal.h"
#include "elog_report.h"

#define ELOG_CFG_ETCD_SERVERS "etcd_servers"
#define ELOG_CFG_ETCD_API_VERSION "etcd_api_version"
#define ELOG_CFG_ETCD_PREFIX "etcd_prefix"
#define ELOG_CFG_ETCD_KEY "etcd_key"
#define ELOG_CFG_ETCD_USER "etcd_user"
#define ELOG_CFG_ETCD_PASSWORD "etcd_password"
#define ELOG_CFG_ETCD_EXPIRE_SECONDS "etcd_expire_seconds"
#define ELOG_CFG_ETCD_RENEW_EXPIRE_SECONDS "etcd_renew_expire_seconds"

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogConfigServiceEtcdPublisher)

ELOG_IMPLEMENT_CONFIG_SERVICE_PUBLISHER(ELogConfigServiceEtcdPublisher)

bool convertEtcdApiVersion(const char* apiVersionStr, ELogEtcdApiVersion& apiVersion) {
    if (strcmp(apiVersionStr, "v2") == 0) {
        apiVersion = ELogEtcdApiVersion::ELOG_ETCD_API_V2;
        return true;
    }
    if (strcmp(apiVersionStr, "v3") == 0) {
        apiVersion = ELogEtcdApiVersion::ELOG_ETCD_API_V3;
        return true;
    }
    ELOG_REPORT_ERROR("Invalid etcd API version string: %s", apiVersionStr);
    return false;
}

ELogConfigServiceEtcdPublisher* ELogConfigServiceEtcdPublisher::create() {
    return new (std::nothrow) ELogConfigServiceEtcdPublisher();
}

void ELogConfigServiceEtcdPublisher::destroy(ELogConfigServiceEtcdPublisher* publisher) {
    if (publisher != nullptr) {
        delete publisher;
    }
}

bool ELogConfigServiceEtcdPublisher::load(const ELogConfigMapNode* cfg) {
    // load everything into parameters object
    ELogConfigServiceEtcdParams params;

    // get mandatory server list
    std::string serverListStr;
    if (!loadEnvCfg(cfg, ELOG_CFG_ETCD_SERVERS, serverListStr, true)) {
        return false;
    }
    if (!parseServerListString(serverListStr)) {
        return false;
    }

    // get optional API version
    std::string apiVersionStr;
    if (!loadEnvCfg(cfg, ELOG_CFG_ETCD_API_VERSION, apiVersionStr, false)) {
        return false;
    }
    if (!apiVersionStr.empty()) {
        if (!convertEtcdApiVersion(apiVersionStr.c_str(), m_params.m_apiVersion)) {
            return false;
        }
    }

    // get optional prefix
    if (!loadEnvCfg(cfg, ELOG_CFG_ETCD_PREFIX, params.m_dirPrefix, false)) {
        return false;
    }

    // get optional key
    if (!loadEnvCfg(cfg, ELOG_CFG_ETCD_KEY, params.m_key, false)) {
        return false;
    }

    // get optional user/password
    if (!loadEnvCfg(cfg, ELOG_CFG_ETCD_USER, params.m_user, false)) {
        return false;
    }
    if (!loadEnvCfg(cfg, ELOG_CFG_ETCD_PASSWORD, params.m_password, false)) {
        return false;
    }

    // redis key expiry in seconds
    if (!loadIntEnvCfg(cfg, ELOG_CFG_ETCD_EXPIRE_SECONDS, params.m_expirySeconds, false)) {
        return false;
    }

    // redis renew expiry in seconds
    if (!loadIntEnvCfg(cfg, ELOG_CFG_ETCD_RENEW_EXPIRE_SECONDS, params.m_renewExpiryTimeoutSeconds,
                       false)) {
        return false;
    }

    // configure and return
    configure(params);
    return true;
}

bool ELogConfigServiceEtcdPublisher::load(const ELogPropertySequence& props) {
    // load everything into parameters object
    ELogConfigServiceEtcdParams params;

    // get mandatory server list
    std::string serverListStr;
    if (!loadEnvProp(props, ELOG_CFG_ETCD_SERVERS, serverListStr, true)) {
        return false;
    }
    if (!parseServerListString(serverListStr)) {
        return false;
    }

    // get optional prefix
    if (!loadEnvProp(props, ELOG_CFG_ETCD_PREFIX, params.m_dirPrefix, false)) {
        return false;
    }

    // get optional key
    if (!loadEnvProp(props, ELOG_CFG_ETCD_KEY, params.m_key, false)) {
        return false;
    }

    // get optional user/password
    if (!loadEnvProp(props, ELOG_CFG_ETCD_USER, params.m_user, false)) {
        return false;
    }
    if (!loadEnvProp(props, ELOG_CFG_ETCD_PASSWORD, params.m_password, false)) {
        return false;
    }

    // configure and return
    configure(params);
    return true;
}

bool ELogConfigServiceEtcdPublisher::initialize() { return initializeEtcd(); }

bool ELogConfigServiceEtcdPublisher::terminate() { return terminateEtcd(); }

void ELogConfigServiceEtcdPublisher::onConfigServiceStart(const char* host, int port) {
    // prepare unique key
    m_serviceSpec = m_params.m_key + ":" + host + ":" + std::to_string(port);

    // start the publish thread
    startPublishThread(m_params.m_renewExpiryTimeoutSeconds);
}

void ELogConfigServiceEtcdPublisher::onConfigServiceStop(const char* host, int port) {
    stopPublishThread();
}

bool ELogConfigServiceEtcdPublisher::publishConfigService() {
    // try to set the key with expiry
    const char* appName = getAppNameField();
    if (appName == nullptr || *appName == 0) {
        appName = getProgramNameField();
    }
    std::string value = std::string(appName);

    if (m_params.m_apiVersion == ELogEtcdApiVersion::ELOG_ETCD_API_V2) {
        return publishConfigServiceV2(value.c_str());
    } else {
        return publishConfigServiceV3(value.c_str());
    }
    return true;
}

void ELogConfigServiceEtcdPublisher::unpublishConfigService() {  // try to remove the key
    if (m_params.m_apiVersion == ELogEtcdApiVersion::ELOG_ETCD_API_V2) {
        unpublishConfigServiceV2();
    } else {
        unpublishConfigServiceV3();
    }
}

void ELogConfigServiceEtcdPublisher::renewExpiry() {
    if (m_params.m_apiVersion == ELogEtcdApiVersion::ELOG_ETCD_API_V2) {
        renewExpiryV2();
    } else {
        renewExpiryV3();
    }
}

bool ELogConfigServiceEtcdPublisher::isConnected() { return isEtcdConnected(); }

bool ELogConfigServiceEtcdPublisher::connect() { return connectEtcd(); }

void ELogConfigServiceEtcdPublisher::embedHeaders(httplib::Headers& headers) {
    if (m_params.m_apiVersion == ELogEtcdApiVersion::ELOG_ETCD_API_V2) {
        // for V2 we embed basic authorization header
        if (!m_params.m_user.empty()) {
            headers.insert(httplib::Headers::value_type("Authorization", m_encodedCredentials));
        }
    } else {
        // for V3 we embed the authentication token received from /v3/auth/authenticate
        headers.insert(httplib::Headers::value_type("Authorization", m_authToken));
    }
}

bool ELogConfigServiceEtcdPublisher::handleResult(const httplib::Result& result) {
    if (m_params.m_apiVersion == ELogEtcdApiVersion::ELOG_ETCD_API_V2) {
        if (result->status == 200 || result->status == 201) {
            // 200 - OK
            // 201 - CREATED
            return true;
        }
    }
    return ELogHttpClientAssistant::handleResult(result);
}

bool ELogConfigServiceEtcdPublisher::publishConfigServiceV2(const char* value) {
    std::string endpoint = std::string("/v2/keys/") + m_dir + m_serviceSpec;
    endpoint += std::string("?value=") + value + "&ttl=" + std::to_string(m_params.m_expirySeconds);
    std::string responseBody;
    std::pair<bool, int> res =
        m_etcdClient.put(endpoint.c_str(), "", 0, "text/plain", false, &responseBody);
    if (!res.first) {
        ELOG_REPORT_ERROR("Failed to set etcd key %s (HTTP status: %d)", endpoint.c_str(),
                          res.second);
        // if the response is empty we know the connection is broken
        if (responseBody.empty()) {
            m_connected = true;
        }
        return false;
    }
    ELOG_REPORT_TRACE("Received response: %s", responseBody.c_str());
    return true;
}

void ELogConfigServiceEtcdPublisher::unpublishConfigServiceV2() {
    std::string endpoint = std::string("/v2/keys/") + m_dir + m_serviceSpec;
    std::string responseBody;
    std::pair<bool, int> res =
        m_etcdClient.del(endpoint.c_str(), "", 0, "text/plain", false, &responseBody);
    if (!res.first) {
        ELOG_REPORT_ERROR("Failed to delete etcd key %s (HTTP status: %d)", endpoint.c_str(),
                          res.second);
    }
    ELOG_REPORT_TRACE("Received response: %s", responseBody.c_str());
}

void ELogConfigServiceEtcdPublisher::renewExpiryV2() {
    std::string endpoint = std::string("/v2/keys/") + m_dir + m_serviceSpec;
    endpoint += std::string("?ttl=") + std::to_string(m_params.m_expirySeconds) + "&refresh=true";
    std::string responseBody;
    std::pair<bool, int> res =
        m_etcdClient.put(endpoint.c_str(), "", 0, "text/plain", false, &responseBody);
    if (!res.first) {
        ELOG_REPORT_ERROR("Failed to refresh etcd key %s (HTTP status: %d)", endpoint.c_str(),
                          res.second);
        // if the response is empty we know the connection is broken
        if (responseBody.empty()) {
            m_connected = false;
        }
        // make sure the key will get published
        setRequiresPublish();
    }
    ELOG_REPORT_TRACE("Received response: %s", responseBody.c_str());
}

bool ELogConfigServiceEtcdPublisher::publishConfigServiceV3(const char* value) {
    if (!m_params.m_user.empty()) {
        if (!getAuthToken()) {
            ELOG_REPORT_ERROR("Failed to retrieve authentication token for user: %s",
                              m_params.m_user.c_str());
            return false;
        }
    }
    if (!grantLease(m_params.m_expirySeconds, m_leaseId)) {
        ELOG_REPORT_ERROR("Failed to grant lease at etcd server");
        return false;
    }
    // 3. put value with lease: /v3/kv/put -d key=key value=value lease=id
    if (!putKeyV3(m_serviceSpec.c_str(), value)) {
        ELOG_REPORT_ERROR("Failed to put ket at etcd server");
        return false;
    }
    return true;
}

void ELogConfigServiceEtcdPublisher::unpublishConfigServiceV3() {
    // NOTE: all endpoints are specified in the gRPC gateway found at rpc.proto of etcd
    // up-to date file can be found at:
    // https://github.com/etcd-io/etcd/blob/main/api/etcdserverpb/rpc.proto

    // prepare json request
    std::string endpoint = std::string("/v3/kv/deleterange");
    nlohmann::json body;
    body["key"] = encodeBase64(m_serviceSpec);

    // send request
    std::string dumpedBody = body.dump();
    std::string responseBody;
    ELOG_REPORT_TRACE("Sending body: %s", dumpedBody.c_str());
    std::pair<bool, int> res =
        m_etcdClient.post(endpoint.c_str(), dumpedBody.c_str(), dumpedBody.size(), "text/plain",
                          false, &responseBody);

    // check response
    ELOG_REPORT_TRACE("Received response: %s", responseBody.c_str());
    if (!res.first) {
        ELOG_REPORT_ERROR("Failed to set etcd key %s (HTTP status: %d)", endpoint.c_str(),
                          res.second);
        // if the response is empty we know the connection is broken
        if (responseBody.empty()) {
            m_connected = false;
        }
    }

    // no response parsing is required
}

void ELogConfigServiceEtcdPublisher::renewExpiryV3() {
    // prepare json request
    std::string endpoint = std::string("/v3/lease/keepalive");
    nlohmann::json body;
    body["ID"] = m_leaseId;

    // send request
    std::string dumpedBody = body.dump();
    ELOG_REPORT_TRACE("Sending body: %s", dumpedBody.c_str());
    std::string responseBody;
    std::pair<bool, int> res =
        m_etcdClient.post(endpoint.c_str(), dumpedBody.c_str(), dumpedBody.size(), "text/plain",
                          false, &responseBody);

    // check response
    ELOG_REPORT_TRACE("Received response: %s", responseBody.c_str());
    if (!res.first) {
        ELOG_REPORT_ERROR("Failed to grant etcd lease (HTTP status: %d)", res.second);
        // if the response is empty we know the connection is broken
        if (responseBody.empty()) {
            m_connected = false;
        }
        // make sure the key will get published
        setRequiresPublish();
        return;
    }

    // parse result and get lease id
    nlohmann::json jsonResponse = nlohmann::json::parse(responseBody);
    if (!jsonResponse.contains("result")) {
        ELOG_REPORT_ERROR("Invalid JSON response from etcd keep-alive lease: missing result");
        return;
    }
    const nlohmann::json& jsonResult = jsonResponse["result"];
    if (!jsonResult.contains("ID")) {
        ELOG_REPORT_ERROR("Invalid JSON response from etcd keep-alive lease: missing ID");
        return;
    }
    int64_t leaseId = 0;
    const std::string& leaseIdStr = jsonResult["ID"];
    // std::string decodedLeaseIdStr = decodeBase64(leaseIdStr).c_str();
    if (!parseIntProp("etcd/v3/lease/keepalive", "", leaseIdStr, leaseId, false)) {
        ELOG_REPORT_ERROR("Invalid etcd response lease id: %s", leaseIdStr.c_str());
        return;
    }
    if (leaseId != m_leaseId) {
        ELOG_REPORT_ERROR("Mismatching lease id, expecting %" PRId64 ", instead got %" PRId64,
                          m_leaseId, leaseId);
        return;
    }

    // now parse TTL
    if (!jsonResult.contains("TTL")) {
        ELOG_REPORT_ERROR("Invalid JSON response from etcd keep-alive lease: missing TTL");
        return;
    }
    int64_t ttl = 0;
    const std::string& ttlStr = jsonResult["TTL"];
    // std::string decodedTtlStr = decodeBase64(ttlStr).c_str();
    if (!parseIntProp("etcd/v3/lease/keepalive", "", ttlStr, ttl, false)) {
        ELOG_REPORT_ERROR("Invalid etcd response lease id: %s", ttlStr.c_str());
        return;
    }
    ELOG_REPORT_TRACE("Got TTL: %" PRId64, ttl);
}

bool ELogConfigServiceEtcdPublisher::getAuthToken() {
    // prepare json request
    std::string endpoint = std::string("/v3/auth/authenticate");
    nlohmann::json body;
    body["name"] = encodeBase64(m_params.m_user);
    body["password"] = encodeBase64(m_params.m_password);

    // send request
    std::string dumpedBody = body.dump();
    ELOG_REPORT_TRACE("Sending body: %s", dumpedBody.c_str());
    std::string responseBody;
    std::pair<bool, int> res =
        m_etcdClient.post(endpoint.c_str(), dumpedBody.c_str(), dumpedBody.size(), "text/plain",
                          false, &responseBody);

    // check response
    ELOG_REPORT_TRACE("Received response: %s", responseBody.c_str());
    if (!res.first) {
        ELOG_REPORT_ERROR("Failed to authenticate etcd user %s (HTTP status: %d)",
                          m_params.m_user.c_str(), res.second);
        // if the response is empty we know the connection is broken
        if (responseBody.empty()) {
            m_connected = false;
        }
        return false;
    }

    // parse result and get authentication token
    nlohmann::json jsonResponse = nlohmann::json::parse(responseBody);
    if (!jsonResponse.contains("token")) {
        ELOG_REPORT_ERROR("Invalid response from etcd authenticate, missing field 'token'");
        return false;
    }
    const std::string& encodedAuthToken = jsonResponse["token"];
    m_authToken = decodeBase64(encodedAuthToken);
    return true;
}

bool ELogConfigServiceEtcdPublisher::grantLease(uint32_t expireSeconds, int64_t& leaseId) {
    // prepare json request
    std::string endpoint = std::string("/v3/lease/grant");
    nlohmann::json body;
    body["TTL"] = expireSeconds;
    body["ID"] = 0;

    // send request
    std::string dumpedBody = body.dump();
    ELOG_REPORT_TRACE("Sending body: %s", dumpedBody.c_str());
    std::string responseBody;
    std::pair<bool, int> res =
        m_etcdClient.post(endpoint.c_str(), dumpedBody.c_str(), dumpedBody.size(), "text/plain",
                          false, &responseBody);

    // check response
    ELOG_REPORT_TRACE("Received response: %s", responseBody.c_str());
    if (!res.first) {
        ELOG_REPORT_ERROR("Failed to grant etcd lease (HTTP status: %d)", res.second);
        // if the response is empty we know the connection is broken
        if (responseBody.empty()) {
            m_connected = false;
        }
        return false;
    }

    // parse result and get lease id
    nlohmann::json jsonResponse = nlohmann::json::parse(responseBody);
    if (jsonResponse.contains("error")) {
        const std::string& errMsg = jsonResponse["error"];
        ELOG_REPORT_ERROR("Failed to grant lease by etcd: %s", errMsg.c_str());
        return false;
    }
    if (!jsonResponse.contains("ID")) {
        ELOG_REPORT_ERROR("Invalid JSON response from etcd grant lease: missing ID");
        return false;
    }
    const std::string& leaseIdStr = jsonResponse["ID"];
    // std::string decodedLeaseIdStr = decodeBase64(leaseIdStr).c_str();
    if (!parseIntProp("etcd/v3/lease/grant", "", leaseIdStr, leaseId, false)) {
        ELOG_REPORT_ERROR("Invalid etcd response lease id: %s", leaseIdStr.c_str());
        return false;
    }
    return true;
}

bool ELogConfigServiceEtcdPublisher::putKeyV3(const char* key, const char* value) {
    // prepare json request
    std::string endpoint = std::string("/v3/kv/put");
    nlohmann::json body;
    body["key"] = encodeBase64(m_serviceSpec);
    body["value"] = encodeBase64(value);
    body["lease"] = m_leaseId;

    // send request
    std::string dumpedBody = body.dump();
    ELOG_REPORT_TRACE("Sending body: %s", dumpedBody.c_str());
    std::string responseBody;
    std::pair<bool, int> res =
        m_etcdClient.post(endpoint.c_str(), dumpedBody.c_str(), dumpedBody.size(), "text/plain",
                          false, &responseBody);

    // check response
    ELOG_REPORT_TRACE("Received response: %s", responseBody.c_str());
    if (!res.first) {
        ELOG_REPORT_ERROR("Failed to set etcd key %s (HTTP status: %d)", endpoint.c_str(),
                          res.second);
        // if the response is empty we know the connection is broken
        if (responseBody.empty()) {
            m_connected = false;
        }
        return false;
    }

    // no response parsing is required
    return true;
}

}  // namespace elog

#endif  // ELOG_ENABLE_CONFIG_PUBLISH_ETCD