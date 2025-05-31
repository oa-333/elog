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
    ELogMonSchemaHandler(const ELogMonSchemaHandler&) = default;
    ELogMonSchemaHandler(ELogMonSchemaHandler&&) = default;

    /** @brief Destructor. */
    ~ELogMonSchemaHandler() final;

    /** @brief Registers predefined target providers. */
    bool registerPredefinedProviders() final;

    /** @brief Register external log monitoring tool target provider. */
    bool registerMonTargetProvider(const char* monitorName, ELogMonTargetProvider* provider);

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
    typedef std::unordered_map<std::string, ELogMonTargetProvider*> ProviderMap;
    ProviderMap m_providerMap;
};

}  // namespace elog

#endif  // __ELOG_MON_SCHEMA_HANDLER_H__