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
    ELogDbSchemaHandler(const ELogDbSchemaHandler&) = default;
    ELogDbSchemaHandler(ELogDbSchemaHandler&&) = default;

    /** @brief Destructor. */
    ~ELogDbSchemaHandler() final;

    /** @brief Registers predefined target providers. */
    bool registerPredefinedProviders() final;

    /** @brief Register external database log target provider. */
    bool registerDbTargetProvider(const char* dbName, ELogDbTargetProvider* provider);

    /**
     * @brief Loads a log target by a specification.
     * @param logTargetCfg The log target string specification.
     * @param targetSpec The log target specification.
     * @return ELogTarget* The resulting log target or null if failed.
     */
    ELogTarget* loadTarget(const std::string& logTargetCfg, const ELogTargetSpec& targetSpec) final;

    /**
     * @brief Loads a log target by a specification.
     * @param logTargetCfg The log target string specification.
     * @param targetNestedSpec The log target nested specification.
     * @return ELogTarget* The resulting log target or null if failed.
     */
    ELogTarget* loadTarget(const std::string& logTargetCfg,
                           const ELogTargetNestedSpec& targetNestedSpec) final;

private:
    typedef std::unordered_map<std::string, ELogDbTargetProvider*> ProviderMap;
    ProviderMap m_providerMap;
};

}  // namespace elog

#endif  // __ELOG_DB_SCHEMA_HANDLER_H__