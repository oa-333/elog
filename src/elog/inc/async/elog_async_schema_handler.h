#ifndef __ELOG_ASYNC_SCHEMA_HANDLER_H__
#define __ELOG_ASYNC_SCHEMA_HANDLER_H__

#include <unordered_map>

#include "elog_async_target_provider.h"
#include "elog_schema_handler.h"

namespace elog {

/** @brief Handler for loading asynchronous log target from configuration. */
class ELOG_API ELogAsyncSchemaHandler : public ELogSchemaHandler {
public:
    ELogAsyncSchemaHandler() : ELogSchemaHandler(SCHEME_NAME) {}
    ELogAsyncSchemaHandler(const ELogAsyncSchemaHandler&) = delete;
    ELogAsyncSchemaHandler(ELogAsyncSchemaHandler&&) = delete;
    ELogAsyncSchemaHandler& operator=(const ELogAsyncSchemaHandler&) = delete;

    static constexpr const char* SCHEME_NAME = "async";

    /** @brief Registers predefined target providers. */
    bool registerPredefinedProviders() final;

    ELOG_DECLARE_SCHEMA_HANDLER(ELogAsyncSchemaHandler)
};

}  // namespace elog

#endif  // __ELOG_ASYNC_SCHEMA_HANDLER_H__