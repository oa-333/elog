#ifndef __ELOG_SCHEMA_MANAGER_H__
#define __ELOG_SCHEMA_MANAGER_H__

#include "elog_schema_handler.h"

namespace elog {
class ELogSchemaManager {
public:
    /** @brief Schema marker string (i.e. "://"). */
    static const char* ELOG_SCHEMA_MARKER;

    /** @brief Schema marker string length. */
    static const uint32_t ELOG_SCHEMA_LEN;

    /** @brief Registers a schema handler by name. */
    static bool registerSchemaHandler(const char* schemeName, ELogSchemaHandler* schemaHandler);

    /** @brief Retrieves a schema handler by name. */
    static ELogSchemaHandler* getSchemaHandler(const char* schemeName);

private:
    static bool initSchemaHandlers();
    static void termSchemaHandlers();

    friend class ELogSystem;
};

}  // namespace elog

#endif  // __ELOG_SCHEMA_MANAGER_H__