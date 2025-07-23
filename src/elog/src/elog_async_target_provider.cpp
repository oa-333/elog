#include "elog_async_target_provider.h"

#include "elog_config_loader.h"
#include "elog_config_parser.h"
#include "elog_report.h"

namespace elog {

ELogTarget* ELogAsyncTargetProvider::loadNestedTarget(const ELogConfigMapNode* logTargetCfg) {
    // load nested target
    const ELogConfigValue* value = logTargetCfg->getValue("log_target");
    if (value == nullptr) {
        ELOG_REPORT_ERROR(
            "Missing specification for nested log target in asynchronous log target (context: %s)",
            logTargetCfg->getFullContext());
        return nullptr;
    }

    // the nested target type may be map (for a single target), or an array of maps (combined)
    if (value->getValueType() == ELogConfigValueType::ELOG_CONFIG_MAP_VALUE) {
        const ELogConfigMapNode* subLogTargetCfg = ((const ELogConfigMapValue*)value)->getMapNode();
        return ELogConfigLoader::loadLogTarget(subLogTargetCfg);
    }

    if (value->getValueType() == ELogConfigValueType::ELOG_CONFIG_ARRAY_VALUE) {
        const ELogConfigArrayNode* arrayNode = ((const ELogConfigArrayValue*)value)->getArrayNode();
        ELogCombinedTarget* combinedTarget = new (std::nothrow) ELogCombinedTarget();
        if (combinedTarget == nullptr) {
            ELOG_REPORT_ERROR("Failed to allocate combined log target, out of memory (context: %s)",
                              logTargetCfg->getFullContext());
            return nullptr;
        }
        for (size_t i = 0; i < arrayNode->getValueCount(); ++i) {
            value = arrayNode->getValueAt(i);
            if (value->getValueType() != ELogConfigValueType::ELOG_CONFIG_MAP_VALUE) {
                ELOG_REPORT_ERROR(
                    "Invalid sub log target type, expecting map, instead seeing %s (context: %s)",
                    configValueTypeToString(value->getValueType()), value->getFullContext());
                delete combinedTarget;
                return nullptr;
            }
            const ELogConfigMapNode* subLogTargetCfg =
                ((const ELogConfigMapValue*)value)->getMapNode();
            ELogTarget* subLogTarget = ELogConfigLoader::loadLogTarget(subLogTargetCfg);
            if (subLogTarget == nullptr) {
                ELOG_REPORT_ERROR(
                    "Failed to load sub log target %zu for combined log target (context: %s)", i,
                    subLogTargetCfg->getFullContext());
                delete combinedTarget;
                return nullptr;
            }
            combinedTarget->addLogTarget(subLogTarget);
        }
        return combinedTarget;
    }

    // we need to support flat string type here (URL style)
    if (value->getValueType() == ELogConfigValueType::ELOG_CONFIG_STRING_VALUE) {
        const ELogConfigStringValue* strValue = (const ELogConfigStringValue*)value;
        ELogTarget* logTarget = ELogConfigLoader::loadLogTarget(strValue->getStringValue());
        if (logTarget == nullptr) {
            ELOG_REPORT_ERROR(
                "Failed to load nested log target by configuration '%s' (context: %s)",
                strValue->getStringValue(), logTargetCfg->getFullContext());
            return nullptr;
        }
    }

    ELOG_REPORT_ERROR(
        "Invalid nested log target value type, expecting map, instead seeing %s (context: %s)",
        configValueTypeToString(value->getValueType()), value->getFullContext());
    return nullptr;
}

}  // namespace elog
