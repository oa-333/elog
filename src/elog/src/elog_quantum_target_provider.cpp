#include "elog_quantum_target_provider.h"

#include "elog_common.h"
#include "elog_config_loader.h"
#include "elog_error.h"
#include "elog_quantum_target.h"

namespace elog {

ELogAsyncTarget* ELogQuantumTargetProvider::loadTarget(const ELogConfigMapNode* logTargetCfg) {
    // make sure that we have quantum_buffer_size and

    // parse quantum buffer size property
    int64_t quantumBufferSize = 0;
    if (!ELogConfigLoader::getLogTargetIntProperty(logTargetCfg, "asynchronous",
                                                   "quantum_buffer_size", quantumBufferSize)) {
        return nullptr;
    }

    // load nested target
    ELogTarget* target = loadNestedTarget(logTargetCfg);
    if (target == nullptr) {
        return nullptr;
    }

    ELogAsyncTarget* asyncTarget =
        new (std::nothrow) ELogQuantumTarget(target, (uint32_t)quantumBufferSize);
    if (asyncTarget == nullptr) {
        ELOG_REPORT_ERROR("Failed to create quantum log target, out of memory");
        delete target;
        return nullptr;
    }
    // NOTE: ELogSystem will configure common properties for this log target
    return asyncTarget;
}

}  // namespace elog
