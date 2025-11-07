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
    ELogNetSchemaHandler() : ELogSchemaHandler(SCHEME_NAME) {}
    ELogNetSchemaHandler(const ELogNetSchemaHandler&) = delete;
    ELogNetSchemaHandler(ELogNetSchemaHandler&&) = delete;
    ELogNetSchemaHandler& operator=(const ELogNetSchemaHandler&) = delete;

    static constexpr const char* SCHEME_NAME = "net";

    /** @brief Registers predefined target providers. */
    bool registerPredefinedProviders() final;

    ELOG_DECLARE_SCHEMA_HANDLER(ELogNetSchemaHandler)
};

}  // namespace elog

#endif  // ELOG_ENABLE_NET

#endif  // __ELOG_NET_SCHEMA_HANDLER_H__