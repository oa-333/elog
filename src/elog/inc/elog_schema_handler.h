#ifndef __ELOG_SCHEMA_HANDLER_H__
#define __ELOG_SCHEMA_HANDLER_H__

#include "elog_config.h"
#include "elog_target.h"
#include "elog_target_provider.h"
#include "elog_target_spec.h"

namespace elog {

/** @brief Interface for loading log targets by a given scheme. */
class ELOG_API ELogSchemaHandler {
public:
    /** @brief Retrieves the scheme name associated with the schema handler. */
    inline const char* getSchemeName() const { return m_schemeName.c_str(); }

    /** @brief Allow derived classes to registers predefined target providers. */
    virtual bool registerPredefinedProviders() { return true; }

    /** @brief Register external target provider. */
    bool registerTargetProvider(const char* typeName, ELogTargetProvider* provider);

    /**
     * @brief Loads a log target from a configuration object.
     * @param logTargetCfg The log target configuration object.
     * @return ELogTarget* The resulting log target or null if failed.
     */
    virtual ELogTarget* loadTarget(const ELogConfigMapNode* logTargetCfg);

    /**
     * @brief Let every schema handler implement object destruction and finally call "delete this".
     */
    virtual void destroy() = 0;

protected:
    ELogSchemaHandler(const char* schemeName) : m_schemeName(schemeName) {}
    ELogSchemaHandler() = delete;
    ELogSchemaHandler(const ELogSchemaHandler&) = delete;
    ELogSchemaHandler(ELogSchemaHandler&&) = delete;
    ELogSchemaHandler& operator=(const ELogSchemaHandler&) = delete;

    /**
     * @brief protected destructor. ELog cannot delete directly, but only through destroy() method,
     * so that each schema handler will be deleted at its origin module (avoid core dump due to heap
     * mixup).
     */
    virtual ~ELogSchemaHandler();

private:
    typedef std::unordered_map<std::string, ELogTargetProvider*> ProviderMap;
    ProviderMap m_providerMap;
    std::string m_schemeName;
};

/**
 * @brief Helper macro for enforcing controlled object destruction. To be placed in the public
 * section of the schema handler class.
 */
#define ELOG_DECLARE_SCHEMA_HANDLER(SchemaHandlerType) \
private:                                               \
    ~SchemaHandlerType() final{};                      \
                                                       \
public:                                                \
    void destroy() final;

/**
 * @brief Helper macro for enforcing controlled object destruction. To be placed in the public
 * section of the schema handler class.
 */
#define ELOG_DECLARE_SCHEMA_HANDLER_OVERRIDE(SchemaHandlerType) \
protected:                                                      \
    ~SchemaHandlerType() override{};                            \
                                                                \
public:                                                         \
    void destroy() override;

/**
 * @brief Helper macro for enforcing controlled object destruction. To be placed in implementation
 * source file of schema handler class.
 */
#define ELOG_IMPLEMENT_SCHEMA_HANDLER(SchemaHandlerType) \
    void SchemaHandlerType::destroy() { delete this; }

}  // namespace elog

#endif  // __ELOG_SCHEMA_HANDLER_H__