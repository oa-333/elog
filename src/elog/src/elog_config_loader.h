#ifndef __ELOG_CONFIG_LOADER_H__
#define __ELOG_CONFIG_LOADER_H__

#include "elog_config.h"
#include "elog_expression.h"
#include "elog_flush_policy.h"
#include "elog_target.h"
#include "elog_target_spec.h"

namespace elog {

class ELogConfigLoader {
public:
    /**
     * @brief Loads ELog properties from file into a vector of lines.
     * @param configPath The configuration file path.
     * @param lines The lines vector given as pairs of line number and line (skipping empty and
     * comment lines).
     * @return true If the operation succeed, otherwise false.
     */
    static bool loadFile(const char* configPath,
                         std::vector<std::pair<uint32_t, std::string>>& lines);

    /**
     * @brief Loads ELog properties from file, including multiline nested specification.
     * @param configPath The configuration file path.
     * @return true If the operation succeed, otherwise false.
     */
    static bool loadFileProperties(const char* configPath, ELogPropertySequence& props);

    /**
     * @brief Adds a log target from target specification (internal use only).
     */
    static ELogTarget* loadLogTarget(const ELogConfigMapNode* logTargetCfg);

    /**
     * @brief Loads a flush policy from target specification using nested style (internal use only).
     */
    static ELogFlushPolicy* loadFlushPolicy(const ELogConfigMapNode* logTargetCfg, bool allowNone,
                                            bool& result);

    /** @brief Loads flush policy from a parsed expression. */
    static ELogFlushPolicy* loadFlushPolicyExpr(const ELogExpression* expr);

    /**
     * @brief Loads a log filter from target specification using nested style (internal use only).
     */
    static ELogFilter* loadLogFilter(const ELogConfigMapNode* logTargetCfg, bool& result);

    /**
     * @brief Loads a log filter from an expression string.
     */
    static ELogFilter* loadLogFilterExprStr(const char* filterExpr);

    static bool getLogTargetStringProperty(const ELogConfigMapNode* logTargetCfg,
                                           const char* scheme, const char* propName,
                                           std::string& propValue);

    static bool getLogTargetIntProperty(const ELogConfigMapNode* logTargetCfg, const char* scheme,
                                        const char* propName, int64_t& propValue);

    static bool getLogTargetBoolProperty(const ELogConfigMapNode* logTargetCfg, const char* scheme,
                                         const char* propName, bool& propValue);

    static bool getOptionalLogTargetStringProperty(const ELogConfigMapNode* logTargetCfg,
                                                   const char* scheme, const char* propName,
                                                   std::string& propValue, bool* found = nullptr);

    static bool getOptionalLogTargetIntProperty(const ELogConfigMapNode* logTargetCfg,
                                                const char* scheme, const char* propName,
                                                int64_t& propValue, bool* found = nullptr);

    static bool getOptionalLogTargetUIntProperty(const ELogConfigMapNode* logTargetCfg,
                                                 const char* scheme, const char* propName,
                                                 uint64_t& propValue, bool* found = nullptr);

    static bool getOptionalLogTargetUInt32Property(const ELogConfigMapNode* logTargetCfg,
                                                   const char* scheme, const char* propName,
                                                   uint32_t& propValue, bool* found = nullptr);

    static bool getOptionalLogTargetBoolProperty(const ELogConfigMapNode* logTargetCfg,
                                                 const char* scheme, const char* propName,
                                                 bool& propValue, bool* found = nullptr);

private:
    static ELogFlushPolicy* loadFlushPolicyExprStr(const char* flushPolicyExpr, bool& result);
    static ELogFlushPolicy* loadFlushPolicy(const ELogConfigMapNode* flushPolicyCfg,
                                            const char* flushPolicyType, bool allowNone,
                                            bool& result);
    static ELogFilter* loadLogFilterExpr(ELogExpression* expr);
    static ELogFilter* loadLogFilter(const ELogConfigMapNode* filterCfg, const char* filterType,
                                     bool& result);

    static bool configureLogTargetCommon(ELogTarget* logTarget,
                                         const ELogConfigMapNode* logTargetCfg);
    static bool applyTargetName(ELogTarget* logTarget, const ELogConfigMapNode* logTargetCfg);
    static bool applyTargetLogLevel(ELogTarget* logTarget, const ELogConfigMapNode* logTargetCfg);
    static bool applyTargetLogFormat(ELogTarget* logTarget, const ELogConfigMapNode* logTargetCfg);
    static bool applyTargetFlushPolicy(ELogTarget* logTarget,
                                       const ELogConfigMapNode* logTargetCfg);
    static bool applyTargetFilter(ELogTarget* logTarget, const ELogConfigMapNode* logTargetCfg);
};

}  // namespace elog

#endif  // __ELOG_CONFIG_LOADER_H__