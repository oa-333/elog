#ifndef __ELOG_SCHEMA_HANDLER_H__
#define __ELOG_SCHEMA_HANDLER_H__

#include "elog_common.h"
#include "elog_target.h"

namespace elog {

/** @brief Interface for loading log targets by a given schema. */
class ELogSchemaHandler {
public:
    ELogSchemaHandler(const ELogSchemaHandler&) = default;
    ELogSchemaHandler(ELogSchemaHandler&&) = default;

    /** @brief Destructor. */
    virtual ~ELogSchemaHandler() {}

    /**
     * @brief Loads a log target by a specification.
     * @param logTargetCfg The log target string specification.
     * @param targetSpec The log target specification.
     * @return ELogTarget* The resulting log target or null if failed.
     */
    virtual ELogTarget* loadTarget(const std::string& logTargetCfg,
                                   const ELogTargetSpec& targetSpec) = 0;

protected:
    ELogSchemaHandler() {}
};

}  // namespace elog

#endif  // __ELOG_SCHEMA_HANDLER_H__