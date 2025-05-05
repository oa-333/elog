#ifndef __ELOG_DB_SCHEMA_HANDLER_H__
#define __ELOG_DB_SCHEMA_HANDLER_H__

#include "elog_schema_handler.h"

namespace elog {

/** @brief Handler for loading DB log target from configuration. */
class ELogDbSchemaHandler : public ELogSchemaHandler {
public:
    ELogDbSchemaHandler() {}
    ELogDbSchemaHandler(const ELogDbSchemaHandler&) = default;
    ELogDbSchemaHandler(ELogDbSchemaHandler&&) = default;

    /** @brief Destructor. */
    ~ELogDbSchemaHandler() final {}

    /**
     * @brief Loads a log target by a specification.
     * @param logTargetCfg The log target string specification.
     * @param targetSpec The log target specification.
     * @return ELogTarget* The resulting log target or null if failed.
     */
    ELogTarget* loadTarget(const std::string& logTargetCfg, const ELogTargetSpec& targetSpec) final;
};

}  // namespace elog

#endif  // __ELOG_DB_SCHEMA_HANDLER_H__