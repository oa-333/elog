#ifndef __ELOG_SCHEMA_HANDLER_H__
#define __ELOG_SCHEMA_HANDLER_H__

#include "elog_config.h"
#include "elog_target.h"
#include "elog_target_spec.h"

namespace elog {

/** @brief Interface for loading log targets by a given scheme. */
class ELOG_API ELogSchemaHandler {
public:
    ELogSchemaHandler(const ELogSchemaHandler&) = default;
    ELogSchemaHandler(ELogSchemaHandler&&) = default;

    /** @brief Destructor. */
    virtual ~ELogSchemaHandler() {}

    /** @brief Registers predefined target providers. */
    virtual bool registerPredefinedProviders() = 0;

    /**
     * @brief Loads a log target from a configuration object.
     * @param logTargetCfg The log target configuration object.
     * @return ELogTarget* The resulting log target or null if failed.
     */
    virtual ELogTarget* loadTarget(const ELogConfigMapNode* logTargetCfg) = 0;

protected:
    ELogSchemaHandler() {}
};

}  // namespace elog

#endif  // __ELOG_SCHEMA_HANDLER_H__