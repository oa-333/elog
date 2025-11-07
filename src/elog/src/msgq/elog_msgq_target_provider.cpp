#include "msgq/elog_msgq_target_provider.h"

#include <cassert>

#include "elog_config_loader.h"
#include "elog_report.h"

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogMsgQTargetProvider)

ELogTarget* ELogMsgQTargetProvider::loadTarget(const ELogConfigMapNode* logTargetCfg) {
    std::string topic;
    if (!ELogConfigLoader::getLogTargetStringProperty(logTargetCfg, "message queue", "msgq_topic",
                                                      topic)) {
        return nullptr;
    }

    std::string headers;
    if (!ELogConfigLoader::getOptionalLogTargetStringProperty(logTargetCfg, "message queue",
                                                              "msgq_headers", headers)) {
        return nullptr;
    }

    return loadMsgQTarget(logTargetCfg, topic, headers);
}

}  // namespace elog
