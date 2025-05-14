#include "elog_async_target_provider.h"

#include "elog_common.h"
#include "elog_error.h"

namespace elog {

ELogTarget* ELogAsyncTargetProvider::loadNestedTarget(const std::string& logTargetCfg,
                                                      const ELogTargetNestedSpec& targetSpec) {
    // load nested target
    ELogTarget* target = nullptr;
    if (targetSpec.m_subSpec.size() > 1) {
        ELogCombinedTarget* combinedTarget = new (std::nothrow) ELogCombinedTarget();
        for (uint32_t i = 0; i < targetSpec.m_subSpec.size(); ++i) {
            ELogTarget* subTarget = loadSingleSubTarget(logTargetCfg, targetSpec.m_subSpec[i]);
            if (subTarget == nullptr) {
                ELOG_REPORT_ERROR("Failed to load sub target %u: %s", i, logTargetCfg.c_str());
                delete target;
                return nullptr;
            }
            combinedTarget->addLogTarget(subTarget);
        }
        target = combinedTarget;
    } else {
        target = loadSingleSubTarget(logTargetCfg, targetSpec.m_subSpec[0]);
    }
    return target;
}

ELogTarget* ELogAsyncTargetProvider::loadSingleSubTarget(const std::string& logTargetCfg,
                                                         const ELogTargetNestedSpec& targetSpec) {
    ELogTarget* target = ELogSystem::loadLogTarget(logTargetCfg, targetSpec);
    if (target == nullptr) {
        ELOG_REPORT_ERROR("Failed to load nested log target by scheme: %s",
                          targetSpec.m_spec.m_scheme.c_str());
        return nullptr;
    }
    return target;
}

}  // namespace elog
