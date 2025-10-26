#ifndef __ELOG_MON_SCHEMA_HANDLER_H__
#define __ELOG_MON_SCHEMA_HANDLER_H__

#include <unordered_map>

#include "elog_mon_target_provider.h"
#include "elog_schema_handler.h"

namespace elog {

/** @brief Handler for loading monitoring tool log target from configuration. */
class ELOG_API ELogMonSchemaHandler : public ELogSchemaHandler {
public:
    ELogMonSchemaHandler() {}
    ELogMonSchemaHandler(const ELogMonSchemaHandler&) = delete;
    ELogMonSchemaHandler(ELogMonSchemaHandler&&) = delete;
    ELogMonSchemaHandler& operator=(const ELogMonSchemaHandler&) = delete;

    /** @brief Registers predefined target providers. */
    bool registerPredefinedProviders() final;

    /** @brief Register external log monitoring tool target provider. */
    bool registerMonTargetProvider(const char* monitorName, ELogMonTargetProvider* provider);

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
    typedef std::unordered_map<std::string, ELogMonTargetProvider*> ProviderMap;
    ProviderMap m_providerMap;

    /** @brief Private destructor, do not allow direct call to delete. */
    ~ELogMonSchemaHandler() final;
};

}  // namespace elog

#endif  // __ELOG_MON_SCHEMA_HANDLER_H__