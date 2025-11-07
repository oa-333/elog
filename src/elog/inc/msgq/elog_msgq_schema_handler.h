#ifndef __ELOG_MSGQ_SCHEMA_HANDLER_H__
#define __ELOG_MSGQ_SCHEMA_HANDLER_H__

#include <unordered_map>

#include "elog_msgq_target_provider.h"
#include "elog_schema_handler.h"

namespace elog {

/** @brief Handler for loading message queue log target from configuration. */
class ELOG_API ELogMsgQSchemaHandler : public ELogSchemaHandler {
public:
    ELogMsgQSchemaHandler() : ELogSchemaHandler(SCHEME_NAME) {}
    ELogMsgQSchemaHandler(const ELogMsgQSchemaHandler&) = delete;
    ELogMsgQSchemaHandler(ELogMsgQSchemaHandler&&) = delete;
    ELogMsgQSchemaHandler& operator=(const ELogMsgQSchemaHandler&) = delete;

    static constexpr const char* SCHEME_NAME = "msgq";

    /** @brief Registers predefined target providers. */
    bool registerPredefinedProviders() final;

    ELOG_DECLARE_SCHEMA_HANDLER(ELogMsgQSchemaHandler)
};

}  // namespace elog

#endif  // __ELOG_MSGQ_SCHEMA_HANDLER_H__