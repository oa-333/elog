#ifndef __ELOG_NET_SCHEMA_HANDLER_H__
#define __ELOG_NET_SCHEMA_HANDLER_H__

#ifdef ELOG_ENABLE_NET

#include <unordered_map>

#include "elog_schema_handler.h"
#include "net/elog_net_target_provider.h"

namespace elog {

/** @brief Handler for loading network log target from configuration. */
class ELOG_API ELogNetSchemaHandler : public ELogSchemaHandler {
public:
    ELogNetSchemaHandler() {}
    ELogNetSchemaHandler(const ELogNetSchemaHandler&) = delete;
    ELogNetSchemaHandler(ELogNetSchemaHandler&&) = delete;
    ELogNetSchemaHandler& operator=(const ELogNetSchemaHandler&) = delete;

    /** @brief Registers predefined target providers. */
    bool registerPredefinedProviders() final;

    /** @brief Register external net target provider. */
    bool registerNetTargetProvider(const char* name, ELogNetTargetProvider* provider);

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
    typedef std::unordered_map<std::string, ELogNetTargetProvider*> ProviderMap;
    ProviderMap m_providerMap;

    /** @brief Private destructor, do not allow direct call to delete. */
    ~ELogNetSchemaHandler() final;
};

}  // namespace elog

#endif  // ELOG_ENABLE_NET

#endif  // __ELOG_NET_SCHEMA_HANDLER_H__