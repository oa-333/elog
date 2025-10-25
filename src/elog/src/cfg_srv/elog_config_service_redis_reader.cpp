#include "cfg_srv/elog_config_service_redis_reader.h"

#ifdef ELOG_ENABLE_CONFIG_PUBLISH_REDIS

#include <cinttypes>

#include "elog_report.h"

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogConfigServiceRedisReader)

bool ELogConfigServiceRedisReader::initialize() { return initializeRedis(); }

bool ELogConfigServiceRedisReader::terminate() { return terminateRedis(); }

bool ELogConfigServiceRedisReader::listServices(ELogConfigServiceMap& serviceMap) {
    // connect on-demand
    if (!m_redisClient.isRedisConnected()) {
        if (!m_redisClient.connectRedis()) {
            // TODO: should attempt reconnect in the background and then read list for better
            // responsiveness in CLI
            return false;
        }
    }

    // list dir contents
    bool done = false;
    uint64_t cursor = 0;
    bool res = true;
    while (!done && res) {
        bool cmdRes = m_redisClient.visitRedisCommand([this, &serviceMap, &cursor, &done,
                                                       &res](redisContext* conext) {
            ELOG_REPORT_TRACE("Executing redis command: SCAN %" PRIu64 " MATCH %s*", cursor,
                              m_params.m_key.c_str());
            redisReply* reply = (redisReply*)redisCommand(conext, "SCAN %" PRIu64 " MATCH %s*",
                                                          cursor, m_params.m_key.c_str());
            if (!m_redisClient.checkReply(reply, REDIS_REPLY_ARRAY)) {
                ELOG_REPORT_ERROR("Failed to scan for ELog configuration services by pattern %s",
                                  m_params.m_key.c_str());
            } else if (!checkScanReply(reply, cursor)) {
                res = false;
            } else {
                readScanCursor(reply, cursor, serviceMap);
                if (cursor == 0) {
                    done = true;
                }
            }
            return reply;
        });

        if (!cmdRes) {
            ELOG_REPORT_ERROR("Failed to list ELog service at Redis set '%s'",
                              m_params.m_key.c_str());
            res = false;
        }
    }

    return res;
}

bool ELogConfigServiceRedisReader::checkScanReply(redisReply* reply, uint64_t& cursor) {
    // two items array, the second item is another array
    if (reply->elements != 2) {
        ELOG_REPORT_ERROR(
            "Unexpected SCAN result, array expected to have two items, instead seeing: %zu",
            reply->elements);
        return false;
    }

    // expecting string representing unsigned 64 bit integer for cursor
    if (reply->element[0]->type != REDIS_REPLY_STRING) {
        ELOG_REPORT_ERROR("Unexpected SCAN result, first array item type is not string: %d",
                          reply->element[0]->type);
        return false;
    }
    char* endPtr = nullptr;
    cursor = std::strtoull(reply->element[0]->str, &endPtr, 10);
    if (endPtr == reply->element[0]->str || errno == ERANGE) {
        // no char parsed
        ELOG_REPORT_ERROR("Invalid cursor value, cannot parse string as integer: %s",
                          reply->element[0]->str);
        return false;
    }

    if (reply->element[1]->type != REDIS_REPLY_ARRAY) {
        ELOG_REPORT_ERROR("Unexpected SCAN result, second array item type is not array: %d",
                          reply->element[1]->type);
        return false;
    }
    return true;
}

void ELogConfigServiceRedisReader::readScanCursor(redisReply* reply, uint64_t cursor,
                                                  ELogConfigServiceMap& serviceMap) {
    for (size_t i = 0; i < reply->element[1]->elements; i++) {
        if (reply->element[1]->element[i]->type != REDIS_REPLY_STRING) {
            ELOG_REPORT_ERROR(
                "Invalid element type in SCAN result array, expecting string, got instead: %d",
                reply->element[1]->element[i]->type);
            break;
        }
        std::string serviceSpec = reply->element[1]->element[i]->str;
        ELOG_REPORT_TRACE("Read SCAN entry: %s", serviceSpec.c_str());
        std::string appName;
        bool res =
            m_redisClient.visitRedisCommand([this, &serviceSpec, &appName](redisContext* context) {
                redisReply* reply =
                    (redisReply*)redisCommand(context, "GET %s", serviceSpec.c_str());
                if (!m_redisClient.getStringReply(reply, appName)) {
                    ELOG_REPORT_ERROR("Failed to retrieve application name for service %s",
                                      serviceSpec.c_str());
                }
                return reply;
            });

        if (res) {
            if (!serviceMap.insert(ELogConfigServiceMap::value_type(serviceSpec, appName)).second) {
                ELOG_REPORT_WARN("Duplicate service entry %s --> %s skipped", serviceSpec.c_str(),
                                 appName.c_str());
            }
        }
    }
}

}  // namespace elog

#endif  // ELOG_ENABLE_CONFIG_PUBLISH_REDIS