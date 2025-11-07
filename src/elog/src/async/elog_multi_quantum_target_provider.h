#ifndef __ELOG_MULTI_QUANTUM_TARGET_PROVIDER_H__
#define __ELOG_MULTI_QUANTUM_TARGET_PROVIDER_H__

#include "async/elog_async_target_provider.h"

namespace elog {

class ELogMultiQuantumTargetProvider : public ELogAsyncTargetProvider {
public:
    ELogMultiQuantumTargetProvider() {}
    ELogMultiQuantumTargetProvider(const ELogMultiQuantumTargetProvider&) = delete;
    ELogMultiQuantumTargetProvider(ELogMultiQuantumTargetProvider&&) = delete;
    ELogMultiQuantumTargetProvider& operator=(const ELogMultiQuantumTargetProvider&) = delete;
    ~ELogMultiQuantumTargetProvider() final {}

    /**
     * @brief Loads a target from configuration object.
     * @param logTargetCfg The configuration object.
     * @return ELogAsyncTarget* The resulting log target, or null if failed.
     */
    ELogTarget* loadTarget(const ELogConfigMapNode* logTargetCfg) final;
};

}  // namespace elog

#endif  // __ELOG_MULTI_QUANTUM_TARGET_PROVIDER_H__