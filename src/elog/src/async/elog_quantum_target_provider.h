#ifndef __ELOG_QUANTUM_TARGET_PROVIDER_H__
#define __ELOG_QUANTUM_TARGET_PROVIDER_H__

#include "async/elog_async_target_provider.h"

namespace elog {

class ELogQuantumTargetProvider : public ELogAsyncTargetProvider {
public:
    ELogQuantumTargetProvider() {}
    ELogQuantumTargetProvider(const ELogQuantumTargetProvider&) = delete;
    ELogQuantumTargetProvider(ELogQuantumTargetProvider&&) = delete;
    ELogQuantumTargetProvider& operator=(const ELogQuantumTargetProvider&) = delete;
    ~ELogQuantumTargetProvider() final {}

    /**
     * @brief Loads a target from configuration object.
     * @param logTargetCfg The configuration object.
     * @return ELogAsyncTarget* The resulting log target, or null if failed.
     */
    ELogAsyncTarget* loadTarget(const ELogConfigMapNode* logTargetCfg) final;
};

}  // namespace elog

#endif  // __ELOG_QUANTUM_TARGET_PROVIDER_H__