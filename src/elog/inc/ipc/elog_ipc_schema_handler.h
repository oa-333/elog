#ifndef __ELOG_IPC_SCHEMA_HANDLER_H__
#define __ELOG_IPC_SCHEMA_HANDLER_H__

#ifdef ELOG_ENABLE_IPC

#include <unordered_map>

#include "elog_schema_handler.h"

namespace elog {

/** @brief Handler for loading IPC log target from configuration. */
class ELOG_API ELogIpcSchemaHandler : public ELogSchemaHandler {
public:
    ELogIpcSchemaHandler() : ELogSchemaHandler(SCHEME_NAME) {}
    ELogIpcSchemaHandler(const ELogIpcSchemaHandler&) = delete;
    ELogIpcSchemaHandler(ELogIpcSchemaHandler&&) = delete;
    ELogIpcSchemaHandler& operator=(const ELogIpcSchemaHandler&) = delete;

    static constexpr const char* SCHEME_NAME = "ipc";

    /** @brief Registers predefined target providers. */
    bool registerPredefinedProviders() final;

    ELOG_DECLARE_SCHEMA_HANDLER(ELogIpcSchemaHandler)
};

}  // namespace elog

#endif  // ELOG_ENABLE_IPC

#endif  // __ELOG_IPC_SCHEMA_HANDLER_H__