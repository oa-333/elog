#ifndef __ELOG_CONFIG_LOADER_H__
#define __ELOG_CONFIG_LOADER_H__

#include "elog_flush_policy.h"
#include "elog_target.h"
#include "elog_target_spec.h"

namespace elog {

class ELogConfigLoader {
public:
    /**
     * @brief Loads ELog properties from file, including multiline nested specification
     * @param configPath The configuration file path.
     * @return true If the operation succeed, otherwise false.
     */
    static bool loadFileProperties(const char* configPath, ELogPropertySequence& props);

    /**
     * @brief Adds a log target from target specification (internal use only).
     */
    static ELogTarget* loadLogTarget(const std::string& logTargetCfg,
                                     const ELogTargetNestedSpec& logTargetNestedSpec,
                                     ELogTargetSpecStyle specStyle);

    /**
     * @brief Loads a flush policy from target specification using nested style (internal use only).
     */
    static ELogFlushPolicy* loadFlushPolicy(const std::string& logTargetCfg,
                                            const ELogTargetNestedSpec& logTargetNestedSpec,
                                            bool allowNone, bool& result);

    /**
     * @brief Loads a log filter from target specification using nested style (internal use only).
     */
    static ELogFilter* loadLogFilter(const std::string& logTargetCfg,
                                     const ELogTargetNestedSpec& logTargetNestedSpec, bool& result);

private:
    static bool configureLogTargetCommon(ELogTarget* logTarget, const std::string& logTargetCfg,
                                         const ELogTargetNestedSpec& logTargetSpec);
    static void applyTargetName(ELogTarget* logTarget, const ELogTargetSpec& logTargetSpec);
    static bool applyTargetLogLevel(ELogTarget* logTarget, const std::string& logTargetCfg,
                                    const ELogTargetSpec& logTargetSpec);
    static bool applyTargetLogFormat(ELogTarget* logTarget, const std::string& logTargetCfg,
                                     const ELogTargetSpec& logTargetSpec);
    static bool applyTargetFlushPolicy(ELogTarget* logTarget, const std::string& logTargetCfg,
                                       const ELogTargetNestedSpec& logTargetSpec);
    static bool applyTargetFilter(ELogTarget* logTarget, const std::string& logTargetCfg,
                                  const ELogTargetNestedSpec& logTargetSpec);
    static ELogTarget* applyCompoundTarget(ELogTarget* logTarget, const std::string& logTargetCfg,
                                           const ELogTargetSpec& logTargetSpec,
                                           bool& errorOccurred);
};

}  // namespace elog

#endif  // __ELOG_CONFIG_LOADER_H__