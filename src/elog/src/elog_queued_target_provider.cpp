#include "elog_queued_target_provider.h"

#include "elog_common.h"
#include "elog_config_loader.h"
#include "elog_error.h"
#include "elog_queued_target.h"

namespace elog {

ELogAsyncTarget* ELogQueuedTargetProvider::loadTarget(const std::string& logTargetCfg,
                                                      const ELogTargetSpec& targetSpec) {
    // not supported
    ELOG_REPORT_ERROR("Loading queued log target from URL style configuration is not supported");
    return nullptr;
}

ELogAsyncTarget* ELogQueuedTargetProvider::loadTarget(const std::string& logTargetCfg,
                                                      const ELogTargetNestedSpec& targetSpec) {
    // make sure that we have queue_batch_size and

    // parse queue batch size property
    ELogPropertyMap::const_iterator itr = targetSpec.m_spec.m_props.find("queue_batch_size");
    if (itr == targetSpec.m_spec.m_props.end()) {
        ELOG_REPORT_ERROR("Missing queue_batch_size property for queued log target specification");
        return nullptr;
    }

    uint32_t queueBatchSize = 0;
    if (!parseIntProp("queue_batch_size", logTargetCfg, itr->second, queueBatchSize)) {
        return nullptr;
    }

    // parse queue timeout millis property
    itr = targetSpec.m_spec.m_props.find("queue_timeout_millis");
    if (itr == targetSpec.m_spec.m_props.end()) {
        ELOG_REPORT_ERROR(
            "Missing queue_timeout_millis property for queued log target specification");
        return nullptr;
    }
    uint32_t queueTimeoutMillis = 0;
    if (!parseIntProp("queue_timeout_millis", logTargetCfg, itr->second, queueTimeoutMillis)) {
        return nullptr;
    }

    // load nested target
    ELogTarget* target = loadNestedTarget(logTargetCfg, targetSpec);
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
