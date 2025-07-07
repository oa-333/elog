#include "elog_queued_target_provider.h"

#include "elog_common.h"
#include "elog_config_loader.h"
#include "elog_error.h"
#include "elog_queued_target.h"

namespace elog {

ELogAsyncTarget* ELogQueuedTargetProvider::loadTarget(const ELogConfigMapNode* logTargetCfg) {
    // make sure that we have queue_batch_size and

    // parse queue batch size property
    int64_t queueBatchSize = 0;
    if (!ELogConfigLoader::getLogTargetIntProperty(logTargetCfg, "asynchronous", "queue_batch_size",
                                                   queueBatchSize)) {
        return nullptr;
    }

    // parse queue timeout millis property
    int64_t queueTimeoutMillis = 0;
    if (!ELogConfigLoader::getLogTargetIntProperty(logTargetCfg, "asynchronous",
                                                   "queue_timeout_millis", queueTimeoutMillis)) {
        return nullptr;
    }

    // load nested target
    ELogTarget* target = loadNestedTarget(logTargetCfg);
    if (target == nullptr) {
        return nullptr;
    }

    ELogAsyncTarget* asyncTarget = new (std::nothrow)
        ELogQueuedTarget(target, (uint32_t)queueBatchSize, (uint32_t)queueTimeoutMillis);
    if (asyncTarget == nullptr) {
        ELOG_REPORT_ERROR("Failed to create queued log target, out of memory");
        delete target;
        return nullptr;
    }
    // NOTE: ELogSystem will configure common properties for this log target
    return asyncTarget;
}

}  // namespace elog
