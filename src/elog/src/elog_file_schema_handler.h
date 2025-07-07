#ifndef __ELOG_FILE_SCHEMA_HANDLER_H__
#define __ELOG_FILE_SCHEMA_HANDLER_H__

#include "elog_schema_handler.h"

namespace elog {

/** @brief Handler for loading internally supported log target from configuration. */
class ELogFileSchemaHandler : public ELogSchemaHandler {
public:
    ELogFileSchemaHandler() {}
    ELogFileSchemaHandler(const ELogFileSchemaHandler&) = default;
    ELogFileSchemaHandler(ELogFileSchemaHandler&&) = default;

    /** @brief Destructor. */
    ~ELogFileSchemaHandler() final {}

    /** @brief Registers predefined target providers. */
    bool registerPredefinedProviders() final { return true; }

    /**
     * @brief Loads a log target from a configuration object.
     * @param logTargetCfg The log target configuration object.
     * @return ELogTarget* The resulting log target or null if failed.
     */
    ELogTarget* loadTarget(const ELogConfigMapNode* logTargetCfg) final;

private:
    ELogTarget* createLogTarget(const std::string& path, int64_t bufferSize, bool useFileLock,
                                int64_t segmentSizeMB, int64_t segmentRingSize,
                                int64_t segmentCount);
};

}  // namespace elog

#endif  // __ELOG_FILE_SCHEMA_HANDLER_H__