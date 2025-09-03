#include "elog_deferred_target_provider.h"

#include "elog_deferred_target.h"
#include "elog_report.h"

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogDeferredTargetProvider)

ELogAsyncTarget* ELogDeferredTargetProvider::loadTarget(const ELogConfigMapNode* logTargetCfg) {
    // load nested target
    ELogTarget* target = loadNestedTarget(logTargetCfg);
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
