#ifndef __ELOG_CONFIG_SERVICE_READER_ETCD_H__
#define __ELOG_CONFIG_SERVICE_READER_ETCD_H__

#ifdef ELOG_ENABLE_CONFIG_PUBLISH_ETCD

#include <string>
#include <vector>

#include "cfg_srv/elog_config_service_etcd_params.h"
#include "cfg_srv/elog_config_service_etcd_user.h"
#include "cfg_srv/elog_config_service_reader.h"
#include "elog_common_def.h"
#include "elog_http_client.h"

namespace elog {

/** @brief Helper class for reading remote configuration service details from etcd. */
class ELOG_API ELogConfigServiceEtcdReader : public ELogConfigServiceReader,
                                             public ELogConfigServiceEtcdUser {
public:
    ELogConfigServiceEtcdReader() : ELogConfigServiceEtcdUser("etcd-config-service-reader") {}
    ELogConfigServiceEtcdReader(ELogConfigServiceEtcdReader&) = delete;
    ELogConfigServiceEtcdReader(ELogConfigServiceEtcdReader&&) = delete;
    ELogConfigServiceEtcdReader& operator=(const ELogConfigServiceEtcdReader&) = delete;
    ~ELogConfigServiceEtcdReader() final {}

    /** @brief Initializes the configuration service publisher. */
    bool initialize() final;

    /** @brief Terminates the configuration service publisher. */
    bool terminate() final;

    /**
     * @brief Lists all services registered under the configured prefix/key.
     * @param serviceMap The resulting service map, mapping from <host>:<ip> to application name.
     * @return The operations's result.
     */
    bool listServices(ELogConfigServiceMap& serviceMap) final;

private:
    bool listServicesV2(ELogConfigServiceMap& serviceMap);
    bool listServicesV3(ELogConfigServiceMap& serviceMap);

    std::string m_minServiceSpec;
    std::string m_maxServiceSpec;
};

}  // namespace elog

#endif  // ELOG_ENABLE_CONFIG_PUBLISH_ETCD

#endif  // __ELOG_CONFIG_SERVICE_READER_ETCD_H__