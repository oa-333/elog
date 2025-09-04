#ifndef __ELOG_MSGQ_SCHEMA_HANDLER_H__
#define __ELOG_MSGQ_SCHEMA_HANDLER_H__

#include <unordered_map>

#include "elog_msgq_target_provider.h"
#include "elog_schema_handler.h"

namespace elog {

/** @brief Handler for loading message queue log target from configuration. */
class ELOG_API ELogMsgQSchemaHandler : public ELogSchemaHandler {
public:
    ELogMsgQSchemaHandler() {}
    ELogMsgQSchemaHandler(const ELogMsgQSchemaHandler&) = delete;
    ELogMsgQSchemaHandler(ELogMsgQSchemaHandler&&) = delete;
    ELogMsgQSchemaHandler& operator=(const ELogMsgQSchemaHandler&) = delete;

    /** @brief Destructor. */
    ~ELogMsgQSchemaHandler() final;

    /** @brief Registers predefined target providers. */
    bool registerPredefinedProviders() final;

    /** @brief Register external message queue log target provider. */
    bool registerMsgQTargetProvider(const char* brokerName, ELogMsgQTargetProvider* provider);

    /**
     * @brief Loads a log target from a configuration object.
     * @param logTargetCfg The log target configuration object.
     * @return ELogTarget* The resulting log target or null if failed.
     */
    ELogTarget* loadTarget(const ELogConfigMapNode* logTargetCfg) final;

private:
    typedef std::unordered_map<std::string, ELogMsgQTargetProvider*> ProviderMap;
    ProviderMap m_providerMap;
};

}  // namespace elog

#endif  // __ELOG_MSGQ_SCHEMA_HANDLER_H__