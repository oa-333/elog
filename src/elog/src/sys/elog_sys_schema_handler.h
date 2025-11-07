#ifndef __ELOG_SYS_SCHEMA_HANDLER_H__
#define __ELOG_SYS_SCHEMA_HANDLER_H__

#include "elog_schema_handler.h"

namespace elog {

/** @brief Handler for loading internally supported log target from configuration. */
class ELogSysSchemaHandler : public ELogSchemaHandler {
public:
    ELogSysSchemaHandler() : ELogSchemaHandler(SCHEME_NAME) {}
    ELogSysSchemaHandler(const ELogSysSchemaHandler&) = delete;
    ELogSysSchemaHandler(ELogSysSchemaHandler&&) = delete;
    ELogSysSchemaHandler& operator=(const ELogSysSchemaHandler&) = delete;

    static constexpr const char* SCHEME_NAME = "sys";

    ELOG_DECLARE_SCHEMA_HANDLER(ELogSysSchemaHandler)

    /** @brief Registers predefined target providers. */
    bool registerPredefinedProviders() final { return true; }

    /**
     * @brief Loads a log target from a configuration object.
     * @param logTargetCfg The log target configuration object.
     * @return ELogTarget* The resulting log target or null if failed.
     */
    ELogTarget* loadTarget(const ELogConfigMapNode* logTargetCfg) final;
};

}  // namespace elog

#endif  // __ELOG_SYS_SCHEMA_HANDLER_H__