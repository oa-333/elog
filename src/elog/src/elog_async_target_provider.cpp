#include "elog_async_target_provider.h"

#include "elog_config_loader.h"
#include "elog_error.h"

namespace elog {

ELogTarget* ELogAsyncTargetProvider::loadNestedTarget(const std::string& logTargetCfg,
                                                      const ELogTargetNestedSpec& targetSpec) {
    // load nested target
    ELogTargetNestedSpec::SubSpecMap::const_iterator itr = targetSpec.m_subSpec.find("log_target");
    if (itr == targetSpec.m_subSpec.end()) {
        ELOG_REPORT_ERROR(
            "Missing specification for nested log target in asynchronous log target: %s",
            logTargetCfg.c_str());
        return nullptr;
    }

    ELogTarget* target = nullptr;
    const ELogTargetNestedSpec::SubSpecList& subSpecList = itr->second;
    if (subSpecList.empty()) {
        ELOG_REPORT_ERROR(
            "Specification list for nested log target, in asynchronous log target, is empty: %s",
            logTargetCfg.c_str());
        return nullptr;
    }

    if (subSpecList.size() > 1) {
        ELogCombinedTarget* combinedTarget = new (std::nothrow) ELogCombinedTarget();
        for (uint32_t i = 0; i < subSpecList.size(); ++i) {
            ELogTarget* subTarget = loadSingleSubTarget(logTargetCfg, subSpecList[i]);
            if (subTarget == nullptr) {
                ELOG_REPORT_ERROR("Failed to load sub target %u: %s", i, logTargetCfg.c_str());
                delete target;
                return nullptr;
            }
            combinedTarget->addLogTarget(subTarget);
        }
        target = combinedTarget;
    } else {
        target = loadSingleSubTarget(logTargetCfg, subSpecList[0]);
    }
    return target;
}

ELogTarget* ELogAsyncTargetProvider::loadSingleSubTarget(const std::string& logTargetCfg,
                                                         const ELogTargetNestedSpec& targetSpec) {
    ELogTarget* target = ELogConfigLoader::loadLogTarget(logTargetCfg, targetSpec,
                                                         ELogTargetSpecStyle::ELOG_STYLE_NESTED);
    if (target == nullptr) {
        ELOG_REPORT_ERROR("Failed to load nested log target by scheme: %s",
                          targetSpec.m_spec.m_scheme.c_str());
        return nullptr;
    }
    return target;
}

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

    // TODO: we need to support flat string type here (URL style)
    if (value->getValueType() == ELogConfigValueType::ELOG_CONFIG_STRING_VALUE) {
        // TODO: implement
    }

    ELOG_REPORT_ERROR(
        "Invalid nested log target value type, expecting map, instead seeing %s (context: %s)",
        configValueTypeToString(value->getValueType()), value->getFullContext());
    return nullptr;
}

}  // namespace elog
