#ifndef __ELOG_SCHEMA_HANDLER_H__
#define __ELOG_SCHEMA_HANDLER_H__

#include "elog_config.h"
#include "elog_target.h"
#include "elog_target_spec.h"

namespace elog {

/** @brief Interface for loading log targets by a given scheme. */
class ELOG_API ELogSchemaHandler {
public:
    ELogSchemaHandler(const ELogSchemaHandler&) = delete;
    ELogSchemaHandler(ELogSchemaHandler&&) = delete;
    ELogSchemaHandler& operator=(const ELogSchemaHandler&) = delete;

    /** @brief Registers predefined target providers. */
    virtual bool registerPredefinedProviders() = 0;

    /**
     * @brief Loads a log target from a configuration object.
     * @param logTargetCfg The log target configuration object.
     * @return ELogTarget* The resulting log target or null if failed.
     */
    virtual ELogTarget* loadTarget(const ELogConfigMapNode* logTargetCfg) = 0;

    /**
     * @brief Let every schema handler implement object destruction and finally call "delete this".
     */
    virtual void destroy() = 0;

protected:
    ELogSchemaHandler() {}

    /**
     * @brief protected destructor. ELog cannot delete directly, but only through destroy() method,
     * so that each schema handler will be deleted at its origin module (avoid core dump due to heap
     * mixup).
     */
    virtual ~ELogSchemaHandler() {}
};

}  // namespace elog

#endif  // __ELOG_SCHEMA_HANDLER_H__