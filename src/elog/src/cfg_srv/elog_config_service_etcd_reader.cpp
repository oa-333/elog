#include "cfg_srv/elog_config_service_etcd_reader.h"

#ifdef ELOG_ENABLE_CONFIG_PUBLISH_ETCD

#include <nlohmann/json.hpp>

#include "elog_common.h"
#include "elog_report.h"

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogConfigServiceEtcdReader)

bool ELogConfigServiceEtcdReader::initialize() {
    m_minServiceSpec = m_params.m_key + ":000.000.000.000:00000";
    m_maxServiceSpec = m_params.m_key + ":999.999.999.999:99999";
    return initializeEtcd();
}

bool ELogConfigServiceEtcdReader::terminate() { return terminateEtcd(); }

bool ELogConfigServiceEtcdReader::listServices(ELogConfigServiceMap& serviceMap) {
    if (m_params.m_apiVersion == ELogEtcdApiVersion::ELOG_ETCD_API_V2) {
        return listServicesV2(serviceMap);
    } else {
        return listServicesV3(serviceMap);
    }
}

bool ELogConfigServiceEtcdReader::listServicesV2(ELogConfigServiceMap& serviceMap) {
    // list dir contents (no sorting, not recursive)
    if (!isEtcdConnected()) {
        if (!connectEtcd()) {
            return false;
        }
    }

    // list directory keys
    std::string endpoint = std::string("/v2/keys/") + m_dir;
    std::string responseBody;
    std::pair<bool, int> res =
        m_etcdClient.get(endpoint.c_str(), "", 0, "text/plain", false, &responseBody);
    if (!res.first) {
        ELOG_REPORT_ERROR("Failed to list etcd directory %s contents (HTTP status: %d)",
                          m_dir.c_str(), res.second);
        // if the response is empty we know the connection is broken
        if (responseBody.empty()) {
            m_connected = false;
        }
        return false;
    }

    // parse response body json
    ELOG_REPORT_TRACE("Received response: %s", responseBody.c_str());
    nlohmann::json jsonResponse = nlohmann::json::parse(responseBody);
    if (!jsonResponse.contains("node")) {
        ELOG_REPORT_ERROR("Invalid JSON response from etcd GET: missing path /node");
        return false;
    }
    if (!jsonResponse["node"].contains("nodes")) {
        // this is not an error but rather empty result
        ELOG_REPORT_TRACE("Empty JSON response from etcd GET: missing path /node/nodes");
        return true;
    }
    const nlohmann::json& nodes = jsonResponse["node"]["nodes"];
    if (!nodes.is_array()) {
        ELOG_REPORT_ERROR("Invalid JSON response from etcd GET: node/nodes is not na array");
        return false;
    }
    std::string prefix = std::string("/") + m_dir;
    for (const nlohmann::json& node : nodes) {
        if (node.contains("key") && node.contains("value")) {
            const std::string& rawKey = node["key"];
            if (rawKey.starts_with(prefix)) {
                std::string key = rawKey.substr(prefix.length());
                const std::string& appName = node["value"];
                ELOG_REPORT_TRACE("Read etcd key: %s, value: %s", key.c_str(), appName.c_str());
                if (!serviceMap.insert(ELogConfigServiceMap::value_type(key, appName)).second) {
                    ELOG_REPORT_WARN("Duplicate service entry %s --> %s skipped", key.c_str(),
                                     appName.c_str());
                }
            }
        }
    }

    return true;
}

bool ELogConfigServiceEtcdReader::listServicesV3(ELogConfigServiceMap& serviceMap) {
    // list dir contents (no sorting, not recursive)
    if (!isEtcdConnected()) {
        if (!connectEtcd()) {
            return false;
        }
    }

    std::string endpoint = std::string("/v3/kv/range");
    nlohmann::json body;
    body["key"] = encodeBase64(m_minServiceSpec);
    body["range_end"] = encodeBase64(m_maxServiceSpec);
    body["limit"] = 0;
    std::string dumpedBody = body.dump();
    ELOG_REPORT_TRACE("Sending body: %s", dumpedBody.c_str());
    std::string responseBody;
    std::pair<bool, int> res =
        m_etcdClient.post(endpoint.c_str(), dumpedBody.c_str(), dumpedBody.size(), "text/plain",
                          false, &responseBody);
    if (!res.first) {
        ELOG_REPORT_ERROR("Failed to list etcd keys (HTTP status: %d)", res.second);
        // if the response is empty we know the connection is broken
        if (responseBody.empty()) {
            m_connected = false;
        }
        return false;
    }

    // parse response
    ELOG_REPORT_TRACE("Received response: %s", responseBody.c_str());
    nlohmann::json jsonResponse = nlohmann::json::parse(responseBody);
    if (!jsonResponse.contains("kvs")) {
        // NOTE: this is not an error, just an empty result set
        ELOG_REPORT_ERROR("Empty JSON response from etcd range query: missing kvs");
        return true;
    }
    if (!jsonResponse["kvs"].is_array()) {
        ELOG_REPORT_TRACE("Invalid JSON response from etcd range query: missing kvs");
        return false;
    }
    for (const auto& entry : jsonResponse["kvs"]) {
        if (!entry.contains("key")) {
            ELOG_REPORT_ERROR(
                "Invalid JSON response from etcd range query: kvs entry missing key property");
            return false;
        }
        if (!entry.contains("value")) {
            ELOG_REPORT_ERROR(
                "Invalid JSON response from etcd range query: kvs entry missing value property");
            return false;
        }
        std::string key = decodeBase64(entry["key"]);
        if (key.starts_with(m_params.m_key)) {
            std::string value = decodeBase64(entry["value"]);
            ELOG_REPORT_TRACE("Read etcd key: %s, value: %s", key.c_str(), value.c_str());
            if (!serviceMap.insert(ELogConfigServiceMap::value_type(key, value)).second) {
                ELOG_REPORT_WARN("Duplicate service entry %s --> %s skipped", key.c_str(),
                                 value.c_str());
            }
        }
    }

    return true;
}

}  // namespace elog

#endif  // ELOG_ENABLE_CONFIG_PUBLISH_ETCD