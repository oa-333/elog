#include "async/elog_quantum_target_provider.h"

#include "async/elog_quantum_target.h"
#include "elog_common.h"
#include "elog_config_loader.h"
#include "elog_report.h"

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogQuantumTargetProvider)

ELogTarget* ELogQuantumTargetProvider::loadTarget(const ELogConfigMapNode* logTargetCfg) {
    // make sure that we have quantum_buffer_size and

    // parse quantum buffer size property
    uint32_t quantumBufferSize = 0;
    if (!ELogConfigLoader::getLogTargetUInt32Property(logTargetCfg, "asynchronous",
                                                      "quantum_buffer_size", quantumBufferSize)) {
        return nullptr;
    }

    // parse quantum collect period (micros)
    uint64_t quantumCollectPeriodMicros = ELOG_DEFAULT_COLLECT_PERIOD_MICROS;
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

    ELogAsyncTarget* asyncTarget =
        new (std::nothrow) ELogQuantumTarget(target, quantumBufferSize, quantumCollectPeriodMicros);
    if (asyncTarget == nullptr) {
        ELOG_REPORT_ERROR("Failed to create quantum log target, out of memory");
        target->destroy();
        return nullptr;
    }
    // NOTE: ELogSystem will configure common properties for this log target
    return asyncTarget;
}

}  // namespace elog
