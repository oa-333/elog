#include "elog_deferred_target_provider.h"

#include "elog_deferred_target.h"
#include "elog_error.h"

namespace elog {

ELogAsyncTarget* ELogDeferredTargetProvider::loadTarget(const std::string& logTargetCfg,
                                                        const ELogTargetSpec& targetSpec) {
    // not supported
    ELOG_REPORT_ERROR("Loading deferred log target from URL style configuration is not supported");
    return nullptr;
}

ELogAsyncTarget* ELogDeferredTargetProvider::loadTarget(const std::string& logTargetCfg,
                                                        const ELogTargetNestedSpec& targetSpec) {
    // load nested target
    ELogTarget* target = loadNestedTarget(logTargetCfg, targetSpec);
    if (target == nullptr) {
        return nullptr;
    }

    ELogAsyncTarget* asyncTarget = new (std::nothrow) ELogDeferredTarget(target);
    if (asyncTarget == nullptr) {
        ELOG_REPORT_ERROR("Failed to create deferred log target, out of memory");
        delete target;
        return nullptr;
    }
    // NOTE: ELogSystem will configure common properties for this log target
    return asyncTarget;
}

}  // namespace elog
