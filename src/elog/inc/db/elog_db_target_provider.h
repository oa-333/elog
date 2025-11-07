#ifndef __ELOG_DB_TARGET_PROVIDER_H__
#define __ELOG_DB_TARGET_PROVIDER_H__

#include "elog_db_target.h"
#include "elog_target_provider.h"
#include "elog_target_spec.h"

namespace elog {

/** @brief Parent interface for all DB log targets. */
class ELOG_API ELogDbTargetProvider : public ELogTargetProvider {
public:
    ELogDbTargetProvider(const ELogDbTargetProvider&) = delete;
    ELogDbTargetProvider(ELogDbTargetProvider&&) = delete;
    ELogDbTargetProvider& operator=(const ELogDbTargetProvider&) = delete;
    virtual ~ELogDbTargetProvider() {}

    /**
     * @brief Loads a target from configuration.
     * @param logTargetCfg The configuration object.
     * @return ELogTarget* The resulting log target, or null of failed.
     */
    ELogTarget* loadTarget(const ELogConfigMapNode* logTargetCfg) override;

protected:
    ELogDbTargetProvider() {}

    /**
     * @brief Loads a target from configuration.
     * @param logTargetCfg The configuration object.
     * @param dbConfig Common database target attributes.
     * @return ELogTarget* The resulting DB log target, or null of failed.
     */
    virtual ELogTarget* loadDbTarget(const ELogConfigMapNode* logTargetCfg,
                                     const ELogDbConfig& dbConfig) = 0;

private:
    bool loadDbAttributes(const ELogConfigMapNode* logTargetCfg, ELogDbConfig& dbConfig);
};

}  // namespace elog

#endif  // __ELOG_DB_TARGET_PROVIDER_H__