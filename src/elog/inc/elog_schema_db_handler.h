#ifndef __ELOG_SCHEMA_DB_HANDLER_H__
#define __ELOG_SCHEMA_DB_HANDLER_H__

#define ELOG_ENABLE_DB_CONNECTOR
#ifdef ELOG_ENABLE_DB_CONNECTOR

#include "elog_schema_handler.h"

namespace elog {
class ELogSchemaDbHandler : public ELogSchemaHandler {
public:
    ELogSchemaDbHandler() {}
    ELogSchemaDbHandler(const ELogSchemaDbHandler&) = default;
    ELogSchemaDbHandler(ELogSchemaDbHandler&&) = default;

    /** @brief Destructor. */
    ~ELogSchemaDbHandler() final {}

    /**
     * @brief Loads a log target by a specification.
     * @param logTargetCfg The log target string specification.
     * @param targetSpec The log target specification.
     * @return ELogTarget* The resulting log target or null if failed.
     */
    ELogTarget* loadTarget(const std::string& logTargetCfg, const ELogTargetSpec& targetSpec) final;
};

}  // namespace elog

#endif  // ELOG_ENABLE_DB_CONNECTOR

#endif  // __ELOG_SCHEMA_DB_HANDLER_H__