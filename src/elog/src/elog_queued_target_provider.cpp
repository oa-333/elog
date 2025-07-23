#include "elog_queued_target_provider.h"

#include "elog_common.h"
#include "elog_config_loader.h"
#include "elog_queued_target.h"
#include "elog_report.h"

namespace elog {

ELogAsyncTarget* ELogQueuedTargetProvider::loadTarget(const ELogConfigMapNode* logTargetCfg) {
    // make sure that we have queue_batch_size and

    // parse queue batch size property
    uint32_t queueBatchSize = 0;
    if (!ELogConfigLoader::getLogTargetUInt32Property(logTargetCfg, "asynchronous",
                                                      "queue_batch_size", queueBatchSize)) {
        return nullptr;
    }

    // parse queue timeout millis property
    uint64_t queueTimeoutMillis = 0;
    if (!ELogConfigLoader::getLogTargetTimeoutProperty(logTargetCfg, "asynchronous",
                                                       "queue_timeout", queueTimeoutMillis,
                                                       ELogTimeoutUnits::TU_MILLI_SECONDS)) {
        return nullptr;
    }

    // load nested target
    ELogTarget* target = loadNestedTarget(logTargetCfg);
    if (target == nullptr) {
        return nullptr;
    }

    ELogAsyncTarget* asyncTarget =
        new (std::nothrow) ELogQueuedTarget(target, queueBatchSize, queueTimeoutMillis);
    if (asyncTarget == nullptr) {
        ELOG_REPORT_ERROR("Failed to create queued log target, out of memory");
        delete target;
        return nullptr;
    }
    // NOTE: ELogSystem will configure common properties for this log target
    return asyncTarget;
}

}  // namespace elog
