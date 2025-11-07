#ifndef __ELOG_FILE_SCHEMA_HANDLER_H__
#define __ELOG_FILE_SCHEMA_HANDLER_H__

#include "elog_schema_handler.h"

namespace elog {

/** @brief Handler for loading internally supported log target from configuration. */
class ELogFileSchemaHandler : public ELogSchemaHandler {
public:
    ELogFileSchemaHandler() : ELogSchemaHandler(SCHEME_NAME) {}
    ELogFileSchemaHandler(const ELogFileSchemaHandler&) = delete;
    ELogFileSchemaHandler(ELogFileSchemaHandler&&) = delete;
    ELogFileSchemaHandler& operator=(ELogFileSchemaHandler&) = delete;

    static constexpr const char* SCHEME_NAME = "file";

    ELOG_DECLARE_SCHEMA_HANDLER(ELogFileSchemaHandler)

    /** @brief Registers predefined target providers. */
    bool registerPredefinedProviders() final { return true; }

    /**
     * @brief Loads a log target from a configuration object.
     * @param logTargetCfg The log target configuration object.
     * @return ELogTarget* The resulting log target or null if failed.
     */
    ELogTarget* loadTarget(const ELogConfigMapNode* logTargetCfg) final;

    /**
     * @brief Create a file log target object.
     *
     * @param path The file path. In case of segmented/rotating file, the containing directory is
     * used for log segments. Log segments name following ordinal number, where first segment has no
     * number, second segment has number 2, and so on.
     * @param bufferSizeBytes File buffer size.
     * @param useFileLock Specifies whether file buffer requires a lock.
     * @param segmentSizeBytes Segment size limit in bytes. Specify zero for not using segmented
     * file log target.
     * @param segmentRingSize The pending log messages ring buffer size used by segmented file
     * target when switching segments.
     * @param segmentCount Segment count limitation, in effect turning the segmented file target
     * into a rotating file target.
     * @param enableStats Specifies whether log target statistics should be collected.
     * @return ELogTarget* The resulting log target or null if failed.
     */
    static ELogTarget* createLogTarget(const std::string& path, uint64_t bufferSizeBytes,
                                       bool useFileLock, uint64_t segmentSizeBytes,
                                       uint32_t segmentRingSize, uint32_t segmentCount,
                                       bool enableStats);
};

}  // namespace elog

#endif  // __ELOG_FILE_SCHEMA_HANDLER_H__