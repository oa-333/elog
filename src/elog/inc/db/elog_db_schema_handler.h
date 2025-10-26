#ifndef __ELOG_DB_SCHEMA_HANDLER_H__
#define __ELOG_DB_SCHEMA_HANDLER_H__

#include <unordered_map>

#include "elog_db_target_provider.h"
#include "elog_schema_handler.h"

namespace elog {

/** @brief Handler for loading DB log target from configuration. */
class ELOG_API ELogDbSchemaHandler : public ELogSchemaHandler {
public:
    ELogDbSchemaHandler() {}
    ELogDbSchemaHandler(const ELogDbSchemaHandler&) = delete;
    ELogDbSchemaHandler(ELogDbSchemaHandler&&) = delete;
    ELogDbSchemaHandler& operator=(const ELogDbSchemaHandler&) = delete;

    /** @brief Registers predefined target providers. */
    bool registerPredefinedProviders() final;

    /** @brief Register external database log target provider. */
    bool registerDbTargetProvider(const char* dbName, ELogDbTargetProvider* provider);

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
    typedef std::unordered_map<std::string, ELogDbTargetProvider*> ProviderMap;
    ProviderMap m_providerMap;

    /** @brief Private destructor, do not allow direct call to delete. */
    ~ELogDbSchemaHandler() final;
};

}  // namespace elog

#endif  // __ELOG_DB_SCHEMA_HANDLER_H__