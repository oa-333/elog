#ifndef __ELOG_SYS_SCHEMA_HANDLER_H__
#define __ELOG_SYS_SCHEMA_HANDLER_H__

#include "elog_schema_handler.h"

namespace elog {

/** @brief Handler for loading internally supported log target from configuration. */
class ELogSysSchemaHandler : public ELogSchemaHandler {
public:
    ELogSysSchemaHandler() {}
    ELogSysSchemaHandler(const ELogSysSchemaHandler&) = delete;
    ELogSysSchemaHandler(ELogSysSchemaHandler&&) = delete;
    ELogSysSchemaHandler& operator=(const ELogSysSchemaHandler&) = delete;

    /** @brief Registers predefined target providers. */
    bool registerPredefinedProviders() final { return true; }

    /**
     * @brief Loads a log target from a configuration object.
     * @param logTargetCfg The log target configuration object.
     * @return ELogTarget* The resulting log target or null if failed.
     */
    ELogTarget* loadTarget(const ELogConfigMapNode* logTargetCfg) final;

    /**
     * @brief Let every schema handler implement object destruction and finally call "delete this".
     */
    void destroy() final;

private:
    /** @brief Private destructor, do not allow direct call to delete. */
    ~ELogSysSchemaHandler() final {}
};

}  // namespace elog

#endif  // __ELOG_SYS_SCHEMA_HANDLER_H__