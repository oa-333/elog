#ifndef __ELOG_PIPE_TARGET_PROVIDER_H__
#define __ELOG_PIPE_TARGET_PROVIDER_H__

#ifdef ELOG_ENABLE_IPC

#include "elog_config.h"
#include "elog_target_provider.h"
#include "elog_target_spec.h"

namespace elog {

/** @brief Parent interface for all IPC log targets. */
class ELOG_API ELogPipeTargetProvider : public ELogTargetProvider {
public:
    ELogPipeTargetProvider(const char* type) : m_type(type) {}
    ELogPipeTargetProvider(const ELogPipeTargetProvider&) = delete;
    ELogPipeTargetProvider(ELogPipeTargetProvider&&) = delete;
    ELogPipeTargetProvider& operator=(const ELogPipeTargetProvider&) = delete;
    ~ELogPipeTargetProvider() final {}

    /**
     * @brief Loads a target from configuration.
     * @param logTargetCfg The configuration string.
     * @return ELogPipeTarget* The resulting IPC log target, or null of failed.
     */
    ELogTarget* loadTarget(const ELogConfigMapNode* logTargetCfg) final;

private:
    std::string m_type;
};

}  // namespace elog

#endif  // ELOG_ENABLE_IPC

#endif  // __ELOG_PIPE_TARGET_PROVIDER_H__