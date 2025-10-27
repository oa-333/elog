#include "async/elog_multi_quantum_target_provider.h"

#include "async/elog_multi_quantum_target.h"
#include "elog_common.h"
#include "elog_config_loader.h"
#include "elog_report.h"

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogMultiQuantumTargetProvider)

ELogAsyncTarget* ELogMultiQuantumTargetProvider::loadTarget(const ELogConfigMapNode* logTargetCfg) {
    // make sure that we have quantum_buffer_size and

    // parse quantum buffer size property
    uint32_t quantumBufferSize = 0;
    if (!ELogConfigLoader::getLogTargetUInt32Property(logTargetCfg, "asynchronous",
                                                      "quantum_buffer_size", quantumBufferSize)) {
        return nullptr;
    }

    // parse quantum reader count
    uint32_t readerCount = ELOG_MQT_DEFAULT_READER_COUNT;
    if (!ELogConfigLoader::getOptionalLogTargetUInt32Property(
            logTargetCfg, "asynchronous", "quantum_reader_count", readerCount)) {
        return nullptr;
    }

    // parse active revisit period
    uint32_t activeRevisitPeriod = ELOG_MQT_DEFAULT_ACTIVE_REVISIT_COUNT;
    if (!ELogConfigLoader::getOptionalLogTargetUInt32Property(
            logTargetCfg, "asynchronous", "quantum_active_revisit_period", activeRevisitPeriod)) {
        return nullptr;
    }

    // parse full revisit period
    uint32_t fullRevisitPeriod = ELOG_MQT_DEFAULT_FULL_REVISIT_COUNT;
    if (!ELogConfigLoader::getOptionalLogTargetUInt32Property(
            logTargetCfg, "asynchronous", "quantum_full_revisit_period", fullRevisitPeriod)) {
        return nullptr;
    }

    // parse max batch size
    uint32_t maxBatchSize = ELOG_MQT_DEFAULT_MAX_BATCH_SIZE;
    if (!ELogConfigLoader::getOptionalLogTargetUInt32Property(logTargetCfg, "asynchronous",
                                                              "quantum_batch_size", maxBatchSize)) {
        return nullptr;
    }

    // parse quantum collect period (micros)
    uint64_t quantumCollectPeriodMicros = ELOG_MQT_DEFAULT_COLLECT_PERIOD_MICROS;
    if (!ELogConfigLoader::getOptionalLogTargetTimeoutProperty(
            logTargetCfg, "asynchronous", "quantum_collect_period", quantumCollectPeriodMicros,
            ELogTimeUnits::TU_MICRO_SECONDS)) {
        return nullptr;
    }

    // load nested target
    ELogTarget* target = loadNestedTarget(logTargetCfg);
    if (target == nullptr) {
        return nullptr;
    }

    ELogAsyncTarget* asyncTarget = new (std::nothrow)
        ELogMultiQuantumTarget(target, quantumBufferSize, readerCount, activeRevisitPeriod,
                               fullRevisitPeriod, maxBatchSize, quantumCollectPeriodMicros);
    if (asyncTarget == nullptr) {
        ELOG_REPORT_ERROR("Failed to create multi quantum log target, out of memory");
        target->destroy();
        return nullptr;
    }
    // NOTE: ELogSystem will configure common properties for this log target
    return asyncTarget;
}

}  // namespace elog
