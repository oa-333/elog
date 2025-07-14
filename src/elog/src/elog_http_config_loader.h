#ifndef __ELOG_HTTP_CONFIG_LOADER_H__
#define __ELOG_HTTP_CONFIG_LOADER_H__

#include "elog_def.h"

#ifdef ELOG_ENABLE_HTTP

#include "elog_config.h"
#include "elog_http_config.h"

namespace elog {

class ELogHttpConfigLoader {
public:
    /**
     * @brief Loads HTTP configuration for a log target.
     * @param logTargetCfg The configuration object.
     * @param targetName The log target name.
     * @param[out] httpConfig The resulting loaded configuration. Make sure to fill in this struct
     * with default values before making the call.
     * @return The operation result.
     */
    static bool loadHttpConfig(const ELogConfigMapNode* logTargetCfg, const char* targetName,
                               ELogHttpConfig& httpConfig);

private:
    ELogHttpConfigLoader() {}
    ELogHttpConfigLoader(const ELogHttpConfigLoader&) = delete;
    ELogHttpConfigLoader(ELogHttpConfigLoader&&) = delete;
    ~ELogHttpConfigLoader() {}

    static bool loadHttpConfigValue(const ELogConfigMapNode* logTargetCfg, const char* targetName,
                                    const char* propName, uint32_t& value);
};

}  // namespace elog

#endif  // ELOG_ENABLE_HTTP

#endif  // __ELOG_HTTP_CONFIG_LOADER_H__