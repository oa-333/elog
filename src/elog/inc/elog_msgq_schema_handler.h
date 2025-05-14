#ifndef __ELOG_MSGQ_SCHEMA_HANDLER_H__
#define __ELOG_MSGQ_SCHEMA_HANDLER_H__

#include <unordered_map>

#include "elog_msgq_target_provider.h"
#include "elog_schema_handler.h"

namespace elog {

/** @brief Handler for loading DB log target from configuration. */
class ELogMsgQSchemaHandler : public ELogSchemaHandler {
public:
    ELogMsgQSchemaHandler() {}
    ELogMsgQSchemaHandler(const ELogMsgQSchemaHandler&) = default;
    ELogMsgQSchemaHandler(ELogMsgQSchemaHandler&&) = default;

    /** @brief Destructor. */
    ~ELogMsgQSchemaHandler() final {}

    /** @brief Registers predefined target providers. */
    bool registerPredefinedProviders() final;

    /** @brief Register external message queue log target provider. */
    bool registerMsgQTargetProvider(const char* brokerName, ELogMsgQTargetProvider* provider);

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
    typedef std::unordered_map<std::string, ELogMsgQTargetProvider*> ProviderMap;
    ProviderMap m_providerMap;
};

}  // namespace elog

#endif  // __ELOG_MSGQ_SCHEMA_HANDLER_H__