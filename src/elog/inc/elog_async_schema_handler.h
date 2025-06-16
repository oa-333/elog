#ifndef __ELOG_ASYNC_SCHEMA_HANDLER_H__
#define __ELOG_ASYNC_SCHEMA_HANDLER_H__

#include <unordered_map>

#include "elog_async_target_provider.h"
#include "elog_schema_handler.h"

namespace elog {

/** @brief Handler for loading asynchronous log target from configuration. */
class ELOG_API ELogAsyncSchemaHandler : public ELogSchemaHandler {
public:
    ELogAsyncSchemaHandler() {}
    ELogAsyncSchemaHandler(const ELogAsyncSchemaHandler&) = default;
    ELogAsyncSchemaHandler(ELogAsyncSchemaHandler&&) = default;

    /** @brief Destructor. */
    ~ELogAsyncSchemaHandler() final;

    /** @brief Registers predefined target providers. */
    bool registerPredefinedProviders() final;

    /** @brief Register external message queue log target provider. */
    bool registerAsyncTargetProvider(const char* asyncName, ELogAsyncTargetProvider* provider);

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

    /**
     * @brief Loads a log target from a configuration object.
     * @param logTargetCfg The log target configuration object.
     * @return ELogTarget* The resulting log target or null if failed.
     */
    ELogTarget* loadTarget(const ELogConfigMapNode* logTargetCfg) final;

private:
    typedef std::unordered_map<std::string, ELogAsyncTargetProvider*> ProviderMap;
    ProviderMap m_providerMap;
};

}  // namespace elog

#endif  // __ELOG_ASYNC_SCHEMA_HANDLER_H__