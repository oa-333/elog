#ifndef __ELOG_CONFIG_SERVICE_READER_H__
#define __ELOG_CONFIG_SERVICE_READER_H__

#ifdef ELOG_ENABLE_CONFIG_SERVICE

#include <string>
#include <unordered_map>

#include "cfg_srv/elog_config_server_details.h"
#include "elog_common_def.h"

namespace elog {

/** @typedef Map of services (host:port --> app-name) */
typedef std::unordered_map<std::string, std::string> ELogConfigServiceMap;

/**
 * @brief Defines the interface for a service reader from a service discovery server so that the
 * CLI can read remote configuration service details.
 */
class ELOG_API ELogConfigServiceReader {
public:
    ELogConfigServiceReader() {}
    ELogConfigServiceReader(ELogConfigServiceReader&) = delete;
    ELogConfigServiceReader(ELogConfigServiceReader&&) = delete;
    ELogConfigServiceReader& operator=(const ELogConfigServiceReader&) = delete;
    virtual ~ELogConfigServiceReader() {}

    /** @brief Initializes the configuration service publisher. */
    virtual bool initialize() = 0;

    /** @brief Terminates the configuration service publisher. */
    virtual bool terminate() = 0;

    /**
     * @brief Lists all services registered under the configured prefix/key.
     * @param serviceMap The resulting service map, mapping from <host>:<ip> to application name.
     * @return The operations's result.
     */
    virtual bool listServices(ELogConfigServiceMap& serviceMap) = 0;
};

}  // namespace elog

#endif  // ELOG_ENABLE_CONFIG_SERVICE

#endif  // __ELOG_CONFIG_SERVICE_REDIS_READER_H__