#ifndef __ELOG_MSG_CONFIG_LOADER_H__
#define __ELOG_MSG_CONFIG_LOADER_H__

#include "elog_def.h"

#ifdef ELOG_ENABLE_MSG

#include "elog_config.h"
#include "msg/elog_msg_config.h"

namespace elog {

class ELogMsgConfigLoader {
public:
    /**
     * @brief Loads message transport configuration for a log target.
     * @param logTargetCfg The configuration object.
     * @param targetName The log target name.
     * @param[out] msgConfig The resulting loaded configuration. Make sure to fill in this struct
     * with default values before making the call.
     * @return The operation result.
     */
    static bool loadMsgConfig(const ELogConfigMapNode* logTargetCfg, const char* targetName,
                              ELogMsgConfig& msgConfig);

private:
    ELogMsgConfigLoader() {}
    ELogMsgConfigLoader(const ELogMsgConfigLoader&) = delete;
    ELogMsgConfigLoader(ELogMsgConfigLoader&&) = delete;
    ELogMsgConfigLoader& operator=(const ELogMsgConfigLoader&) = delete;
    ~ELogMsgConfigLoader() {}
};

}  // namespace elog

#endif  // ELOG_ENABLE_MSG

#endif  // __ELOG_MSG_CONFIG_LOADER_H__