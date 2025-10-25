#include "cfg_srv/elog_config_service_publisher.h"

#ifdef ELOG_ENABLE_CONFIG_SERVICE

#include <cinttypes>
#include <unordered_map>

#include "cfg_srv/elog_config_service_internal.h"
#include "elog_common.h"
#include "elog_config_parser.h"
#include "elog_report.h"

/** @def The maximum number of flush policies types that can be defined in the system. */
#define ELOG_MAX_CONFIG_SERVICE_PUBLISHER_COUNT 100

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogConfigServicePublisher)

// implement configuration service publisher factory by name with static registration
struct ELogConfigServicePublisherNameConstructor {
    const char* m_name;
    ELogConfigServicePublisherConstructor* m_ctor;
};

static ELogConfigServicePublisherNameConstructor
    sConfigServicePublisherConstructors[ELOG_MAX_CONFIG_SERVICE_PUBLISHER_COUNT] = {};
static uint32_t sConfigServicePublisherConstructorsCount = 0;

typedef std::unordered_map<std::string, ELogConfigServicePublisherConstructor*>
    ELogConfigServicePublisherConstructorMap;

static ELogConfigServicePublisherConstructorMap sConfigServicePublisherConstructorMap;

void registerConfigServicePublisherConstructor(const char* name,
                                               ELogConfigServicePublisherConstructor* constructor) {
    // due to c runtime issues we delay access to unordered map
    if (sConfigServicePublisherConstructorsCount >= ELOG_MAX_CONFIG_SERVICE_PUBLISHER_COUNT) {
        ELOG_REPORT_ERROR(
            "Cannot register configuration service publisher constructor, no space: %s", name);
        exit(1);
    } else {
        sConfigServicePublisherConstructors[sConfigServicePublisherConstructorsCount++] = {
            name, constructor};
    }
}

static bool applyConfigServicePublisherConstructorRegistration() {
    for (uint32_t i = 0; i < sConfigServicePublisherConstructorsCount; ++i) {
        ELogConfigServicePublisherNameConstructor& nameCtorPair =
            sConfigServicePublisherConstructors[i];
        if (!sConfigServicePublisherConstructorMap
                 .insert(ELogConfigServicePublisherConstructorMap::value_type(nameCtorPair.m_name,
                                                                              nameCtorPair.m_ctor))
                 .second) {
            ELOG_REPORT_ERROR("Duplicate configuration service publisher identifier: %s",
                              nameCtorPair.m_name);
            return false;
        }
    }
    return true;
}

bool initConfigServicePublishers() { return applyConfigServicePublisherConstructorRegistration(); }

void termConfigServicePublishers() { sConfigServicePublisherConstructorMap.clear(); }

ELogConfigServicePublisher* constructConfigServicePublisher(const char* name) {
    ELogConfigServicePublisherConstructorMap::iterator itr =
        sConfigServicePublisherConstructorMap.find(name);
    if (itr == sConfigServicePublisherConstructorMap.end()) {
        ELOG_REPORT_ERROR("Invalid configuration service publisher %s: not found", name);
        return nullptr;
    }

    ELogConfigServicePublisherConstructor* constructor = itr->second;
    ELogConfigServicePublisher* configServicePublisher =
        constructor->constructConfigServicePublisher();
    if (configServicePublisher == nullptr) {
        ELOG_REPORT_ERROR("Failed to create configuration service publisher, out of memory");
    }
    return configServicePublisher;
}

bool ELogConfigServicePublisher::loadCfg(const ELogConfigMapNode* cfg, const char* propName,
                                         std::string& value, bool isMandatory) {
    bool found = false;
    if (!cfg->getStringValue(propName, found, value)) {
        ELOG_REPORT_ERROR("Failed to load %s configuration service publisher, error in property %s",
                          getName(), propName);
        return false;
    }
    if (!found) {
        if (isMandatory) {
            ELOG_REPORT_ERROR("Missing property %s for %s configuration service publisher",
                              propName, getName());
            return false;
        } else if (!value.empty()) {
            ELOG_REPORT_NOTICE(
                "Missing property %s for %s configuration service publisher, default value will "
                "be used: %s",
                propName, getName(), value.c_str());
        }
    }
    return true;
}

bool ELogConfigServicePublisher::loadIntCfg(const ELogConfigMapNode* cfg, const char* propName,
                                            uint32_t& value, bool isMandatory) {
    bool found = false;
    int64_t value64 = 0;
    if (!cfg->getIntValue(propName, found, value64)) {
        ELOG_REPORT_ERROR("Failed to load %s configuration service publisher, error in property %s",
                          getName(), propName);
        return false;
    }
    if (!found) {
        if (isMandatory) {
            ELOG_REPORT_ERROR("Missing property %s for %s configuration service publisher",
                              propName, getName());
            return false;
        } else {
            ELOG_REPORT_NOTICE(
                "Missing property %s for %s configuration service publisher, default value will "
                "be used: %u",
                propName, getName(), value);
        }
    } else if (value64 > UINT_MAX || value64 < 0) {
        ELOG_REPORT_ERROR(
            "Property %s for %s configuration service publisher out of range [0, %u]: %" PRId64,
            propName, getName(), UINT_MAX, value64);
        return false;
    }
    return true;
}

bool ELogConfigServicePublisher::loadBoolCfg(const ELogConfigMapNode* cfg, const char* propName,
                                             bool& value, bool isMandatory) {
    bool found = false;
    if (!cfg->getBoolValue(propName, found, value)) {
        ELOG_REPORT_ERROR("Failed to load %s configuration service publisher, error in property %s",
                          getName(), propName);
        return false;
    }
    if (!found) {
        if (isMandatory) {
            ELOG_REPORT_ERROR("Missing property %s for %s configuration service publisher",
                              propName, getName());
            return false;
        } else {
            ELOG_REPORT_NOTICE(
                "Missing property %s for %s configuration service publisher, default value will "
                "be used: %s",
                propName, getName(), value ? "yes" : "no");
        }
    }
    return true;
}

bool ELogConfigServicePublisher::loadProp(const ELogPropertySequence& props, const char* propName,
                                          std::string& value, bool isMandatory) {
    if (!getProp(props, propName, value)) {
        if (isMandatory) {
            ELOG_REPORT_ERROR("Missing property %s for %s configuration service publisher",
                              propName, getName());
            return false;
        } else if (!value.empty()) {
            ELOG_REPORT_NOTICE(
                "Missing property %s for %s configuration service publisher, default value will "
                "be used: %s",
                propName, getName(), value.c_str());
        }
    }
    return true;
}

bool ELogConfigServicePublisher::loadIntProp(const ELogPropertySequence& props,
                                             const char* propName, uint32_t& value,
                                             bool isMandatory) {
    bool found = false;
    if (!getIntProp(props, propName, value, &found)) {
        return false;
    }
    if (isMandatory && !found) {
        ELOG_REPORT_ERROR("Missing property %s for %s configuration service publisher", propName,
                          getName());
        return false;
    } else if (!found) {
        ELOG_REPORT_NOTICE(
            "Missing property %s for %s configuration service publisher, default value will "
            "be used: %u",
            propName, getName(), value);
    }
    return true;
}

bool ELogConfigServicePublisher::loadBoolProp(const ELogPropertySequence& props,
                                              const char* propName, bool& value, bool isMandatory) {
    bool found = false;
    if (!getBoolProp(props, propName, value, &found)) {
        return false;
    }
    if (isMandatory && !found) {
        ELOG_REPORT_ERROR("Missing property %s for %s configuration service publisher", propName,
                          getName());
        return false;
    } else if (!found) {
        ELOG_REPORT_NOTICE(
            "Missing property %s for %s configuration service publisher, default value will "
            "be used: %s",
            propName, getName(), value ? "yes" : "no");
    }
    return true;
}

bool ELogConfigServicePublisher::loadEnvCfg(const ELogConfigMapNode* cfg, const char* propName,
                                            std::string& value, bool mandatory) {
    if (!getStringEnv(propName, value)) {
        if (!loadCfg(cfg, propName, value, mandatory)) {
            return false;
        }
    }
    return true;
}

bool ELogConfigServicePublisher::loadIntEnvCfg(const ELogConfigMapNode* cfg, const char* propName,
                                               uint32_t& value, bool mandatory) {
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

bool ELogConfigServicePublisher::loadBoolEnvCfg(const ELogConfigMapNode* cfg, const char* propName,
                                                bool& value, bool mandatory) {
    bool found = false;
    if (!getBoolEnv(propName, value, true, &found)) {
        return false;
    }
    if (!found) {
        if (!loadBoolCfg(cfg, propName, value, mandatory)) {
            return false;
        }
    }
    return true;
}

bool ELogConfigServicePublisher::loadEnvProp(const ELogPropertySequence& props,
                                             const char* propName, std::string& value,
                                             bool mandatory) {
    if (!getStringEnv(propName, value)) {
        if (!loadProp(props, propName, value, mandatory)) {
            return false;
        }
    }
    return true;
}

bool ELogConfigServicePublisher::loadIntEnvProp(const ELogPropertySequence& props,
                                                const char* propName, uint32_t& value,
                                                bool mandatory) {
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

bool ELogConfigServicePublisher::loadBoolEnvProp(const ELogPropertySequence& props,
                                                 const char* propName, bool& value,
                                                 bool mandatory) {
    bool found = false;
    if (!getBoolEnv(propName, value, true, &found)) {
        return false;
    }
    if (!found) {
        if (!loadBoolProp(props, propName, value, mandatory)) {
            return false;
        }
    }
    return true;
}

void ELogConfigServicePublisher::startPublishThread(uint32_t renewExpiryTimeoutSeconds) {
    m_stopPublish = false;
    m_requiresPublish = true;
    m_publishThread =
        std::thread(&ELogConfigServicePublisher::publishThread, this, renewExpiryTimeoutSeconds);
}

void ELogConfigServicePublisher::stopPublishThread() {
    // stop publish thread
    {
        std::unique_lock<std::mutex> lock(m_lock);
        m_stopPublish = true;
        m_cv.notify_one();
    }
    m_publishThread.join();
}

void ELogConfigServicePublisher::publishThread(uint32_t renewExpiryTimeoutSeconds) {
    std::unique_lock<std::mutex> lock(m_lock);
    while (!m_stopPublish) {
        m_cv.wait_for(lock, std::chrono::seconds(renewExpiryTimeoutSeconds),
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
    if (isConnected()) {
        unpublishConfigService();
    }
}

void ELogConfigServicePublisher::execPublishService() {
    // if not connected then reconnect first
    if (!isConnected()) {
        if (!connect()) {
            return;
        }
        ELOG_REPORT_INFO("Configuration service publisher was able to connect to %s server",
                         getName());
        m_requiresPublish = true;
    }

    // publish if required, otherwise renew expiry of publish key
    if (m_requiresPublish) {
        if (publishConfigService()) {
            m_requiresPublish = false;
        }
    } else {
        renewExpiry();

        // don't wait for next round, publish now
        if (m_requiresPublish) {
            publishConfigService();
        }
    }
}

}  // namespace elog

#endif  // ELOG_ENABLE_CONFIG_SERVICE