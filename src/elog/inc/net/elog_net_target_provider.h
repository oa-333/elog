#ifndef __ELOG_NET_TARGET_PROVIDER_H__
#define __ELOG_NET_TARGET_PROVIDER_H__

#ifdef ELOG_ENABLE_NET

#include "elog_config.h"
#include "elog_target.h"
#include "elog_target_provider.h"
#include "elog_target_spec.h"

namespace elog {

/** @brief Parent interface for all network log targets. */
class ELOG_API ELogNetTargetProvider : public ELogTargetProvider {
public:
    ELogNetTargetProvider(const char* type) : m_type(type) {}
    ELogNetTargetProvider(const ELogNetTargetProvider&) = delete;
    ELogNetTargetProvider(ELogNetTargetProvider&&) = delete;
    ELogNetTargetProvider& operator=(const ELogNetTargetProvider&) = delete;
    ~ELogNetTargetProvider() override {}

    /**
     * @brief Loads a target from configuration.
     * @param logTargetCfg The configuration string.
     * @return ELogNetTarget* The resulting network log target, or null of failed.
     */
    ELogTarget* loadTarget(const ELogConfigMapNode* logTargetCfg) override;

private:
    std::string m_type;
};

}  // namespace elog

#endif  // ELOG_ENABLE_NET

#endif  // __ELOG_NET_TARGET_PROVIDER_H__