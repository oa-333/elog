#include "elog_async_target_provider.h"

#include "elog_common.h"
#include "elog_error.h"

namespace elog {

ELogTarget* ELogAsyncTargetProvider::loadNestedTarget(const std::string& logTargetCfg,
                                                      const ELogTargetNestedSpec& targetSpec) {
    // load nested target
    ELogTargetNestedSpec::SubSpecMap::const_iterator itr = targetSpec.m_subSpec.find("log_target");
    if (itr == targetSpec.m_subSpec.end()) {
        ELOG_REPORT_ERROR(
            "Missing specification for nested log target in asynchronous log target: %s",
            logTargetCfg.c_str());
        return nullptr;
    }

    ELogTarget* target = nullptr;
    const ELogTargetNestedSpec::SubSpecList& subSpecList = itr->second;
    if (subSpecList.empty()) {
        ELOG_REPORT_ERROR(
            "Specification list for nested log target, in asynchronous log target, is empty: %s",
            logTargetCfg.c_str());
        return nullptr;
    }

    if (subSpecList.size() > 1) {
        ELogCombinedTarget* combinedTarget = new (std::nothrow) ELogCombinedTarget();
        for (uint32_t i = 0; i < subSpecList.size(); ++i) {
            ELogTarget* subTarget = loadSingleSubTarget(logTargetCfg, subSpecList[i]);
            if (subTarget == nullptr) {
                ELOG_REPORT_ERROR("Failed to load sub target %u: %s", i, logTargetCfg.c_str());
                delete target;
                return nullptr;
            }
            combinedTarget->addLogTarget(subTarget);
        }
        target = combinedTarget;
    } else {
        target = loadSingleSubTarget(logTargetCfg, subSpecList[0]);
    }
    return target;
}

ELogTarget* ELogAsyncTargetProvider::loadSingleSubTarget(const std::string& logTargetCfg,
                                                         const ELogTargetNestedSpec& targetSpec) {
    ELogTarget* target = ELogSystem::loadLogTarget(logTargetCfg, targetSpec, ELOG_STYLE_NESTED);
    if (target == nullptr) {
        ELOG_REPORT_ERROR("Failed to load nested log target by scheme: %s",
                          targetSpec.m_spec.m_scheme.c_str());
        return nullptr;
    }
    return target;
}

}  // namespace elog
