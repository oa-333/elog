#ifndef __ELOG_DB_SCHEMA_HANDLER_H__
#define __ELOG_DB_SCHEMA_HANDLER_H__

#include "elog_schema_handler.h"

namespace elog {

/** @brief Handler for loading DB log target from configuration. */
class ELOG_API ELogDbSchemaHandler : public ELogSchemaHandler {
public:
    ELogDbSchemaHandler() : ELogSchemaHandler(SCHEME_NAME) {}
    ELogDbSchemaHandler(const ELogDbSchemaHandler&) = delete;
    ELogDbSchemaHandler(ELogDbSchemaHandler&&) = delete;
    ELogDbSchemaHandler& operator=(const ELogDbSchemaHandler&) = delete;

    static constexpr const char* SCHEME_NAME = "db";

    /** @brief Registers predefined target providers. */
    bool registerPredefinedProviders() final;

    ELOG_DECLARE_SCHEMA_HANDLER(ELogDbSchemaHandler)
};

}  // namespace elog

#endif  // __ELOG_DB_SCHEMA_HANDLER_H__