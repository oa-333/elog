#include "cfg_srv/elog_config_service_publisher.h"

#ifdef ELOG_ENABLE_CONFIG_SERVICE

#include <unordered_map>

#include "cfg_srv/elog_config_service_internal.h"
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

}  // namespace elog

#endif  // ELOG_ENABLE_CONFIG_SERVICE