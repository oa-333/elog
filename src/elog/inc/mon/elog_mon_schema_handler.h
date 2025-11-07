#ifndef __ELOG_MON_SCHEMA_HANDLER_H__
#define __ELOG_MON_SCHEMA_HANDLER_H__

#include "elog_schema_handler.h"

namespace elog {

/** @brief Handler for loading monitoring tool log target from configuration. */
class ELOG_API ELogMonSchemaHandler : public ELogSchemaHandler {
public:
    ELogMonSchemaHandler() : ELogSchemaHandler(SCHEME_NAME) {}
    ELogMonSchemaHandler(const ELogMonSchemaHandler&) = delete;
    ELogMonSchemaHandler(ELogMonSchemaHandler&&) = delete;
    ELogMonSchemaHandler& operator=(const ELogMonSchemaHandler&) = delete;

    static constexpr const char* SCHEME_NAME = "mon";

    /** @brief Registers predefined target providers. */
    bool registerPredefinedProviders() final;

    ELOG_DECLARE_SCHEMA_HANDLER(ELogMonSchemaHandler)
};

}  // namespace elog

#endif  // __ELOG_MON_SCHEMA_HANDLER_H__