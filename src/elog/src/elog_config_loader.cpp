#include "elog_config_loader.h"

#include <cassert>
#include <fstream>

#include "elog_common.h"
#include "elog_config_parser.h"
#include "elog_expression_parser.h"
#include "elog_filter.h"
#include "elog_formatter.h"
#include "elog_report.h"
#include "elog_schema_manager.h"

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogConfigLoader)

inline void appendMultiLine(std::string& multiLine, const std::string& line) {
    if (!multiLine.empty()) {
        multiLine += " ";
    }
    multiLine += line;
}

bool ELogConfigLoader::loadFile(const char* configPath,
                                std::vector<std::pair<uint32_t, std::string>>& lines) {
    // use simple format
    std::ifstream cfgFile(configPath);
    if (!cfgFile.good()) {
        ELOG_REPORT_SYS_ERROR(fopen, "Failed to open configuration file for reading: %s",
                              configPath);
        return false;
    }

    uint32_t lineNumber = 0;
    std::string line;
    while (std::getline(cfgFile, line)) {
        ++lineNumber;

        // skip empty lines and full comment lines
        std::string trimmedLine = trim(line);
        if (trimmedLine.empty() || trimmedLine[0] == '#') {
            continue;
        }

        // remove comment part (could be whole line or just end of line)
        std::string::size_type poundPos = line.find('#');
        if (poundPos != std::string::npos) {
            line = line.substr(0, poundPos);
            // no further trimming, otherwise we lose precise location information
        }
        assert(!line.empty());
        lines.push_back({lineNumber, line});
    }

    return true;
}

bool ELogConfigLoader::loadFileProperties(const char* configPath, ELogPropertySequence& props) {
    // use simple format
    std::ifstream cfgFile(configPath);
    if (!cfgFile.good()) {
        ELOG_REPORT_SYS_ERROR(fopen, "Failed to open configuration file for reading: %s",
                              configPath);
        return false;
    }

    uint32_t lineNumber = 0;
    std::string line;
    uint64_t openBraceCount = 0;
    uint64_t closeBraceCount = 0;
    std::string multiLine;
    while (std::getline(cfgFile, line)) {
        ++lineNumber;
        line = trim(line);

        // remove comment part (could be whole line or just end of line)
        std::string::size_type poundPos = line.find('#');
        if (poundPos != std::string::npos) {
            line = line.substr(0, poundPos);
            line = trim(line);  // trim again (perhaps there is white space at end of line)
        }

        // skip empty lines
        if (line.empty()) {
            continue;
        }

        // first check for multi-line chars
        openBraceCount += (uint64_t)std::count(line.begin(), line.end(), '{');
        closeBraceCount += (uint64_t)std::count(line.begin(), line.end(), '}');

        // check for ill-formed braces
        if (openBraceCount < closeBraceCount) {
            ELOG_REPORT_ERROR(
                "Invalid multiline nested log target specification, ill-formed braces: %s (line "
                "%u)",
                line.c_str(), lineNumber);
            return false;
        }

        // check if starting or continuing multi-line
        if (openBraceCount > closeBraceCount) {
            appendMultiLine(multiLine, line);
            continue;
        }

        // check if finished multi0line
        if (openBraceCount == closeBraceCount && !multiLine.empty()) {
            appendMultiLine(multiLine, line);
            line = multiLine;
            multiLine.clear();
        }

        // now parse line
        std::istringstream is_line(line);
        std::string key;
        if (std::getline(is_line, key, '=')) {
            std::string value;
            if (std::getline(is_line, value)) {
                std::string trimmedKey = trim(key);
                std::string trimmedValue = trim(value);
                props.push_back(std::make_pair(trimmedKey, trimmedValue));
            }
        }
    }

    return true;
}

ELogTarget* ELogConfigLoader::loadLogTarget(const char* logTargetCfg) {
    ELogConfig* logTargetConfig = ELogConfigParser::parseLogTargetConfig(logTargetCfg);
    if (logTargetConfig == nullptr) {
        ELOG_REPORT_ERROR("Failed to parse log target URL: %s", logTargetCfg);
        return nullptr;
    }

    const ELogConfigNode* rootNode = logTargetConfig->getRootNode();
    if (rootNode->getNodeType() != ELogConfigNodeType::ELOG_CONFIG_MAP_NODE) {
        ELOG_REPORT_ERROR("Invalid node type, expecting map node, seeing instead %s (context: %s)",
                          configNodeTypeToString(rootNode->getNodeType()),
                          rootNode->getFullContext());
        delete logTargetConfig;
        return nullptr;
    }

    const ELogConfigMapNode* mapNode = (const ELogConfigMapNode*)rootNode;
    ELogTarget* logTarget = loadLogTarget(mapNode);

    delete logTargetConfig;
    return logTarget;
}

ELogTarget* ELogConfigLoader::loadLogTarget(const ELogConfigMapNode* logTargetCfg) {
    // get the scheme type
    std::string scheme;
    bool found = false;
    if (!logTargetCfg->getStringValue("scheme", found, scheme)) {
        ELOG_REPORT_ERROR("Invalid log target configuration, scheme key is invalid (context: %s)",
                          logTargetCfg->getFullContext());
        return nullptr;
    }
    if (!found) {
        ELOG_REPORT_ERROR("Invalid log target configuration, missing scheme key (context: %s)",
                          logTargetCfg->getFullContext());
        return nullptr;
    }
    ELogSchemaHandler* schemaHandler = ELogSchemaManager::getSchemaHandler(scheme.c_str());
    if (schemaHandler == nullptr) {
        ELOG_REPORT_ERROR("Invalid log target specification, unrecognized scheme %s (context: %s)",
                          scheme.c_str(), logTargetCfg->getFullContext());
        return nullptr;
    }

    ELogTarget* logTarget = schemaHandler->loadTarget(logTargetCfg);
    if (logTarget == nullptr) {
        ELOG_REPORT_ERROR("Failed to load target for scheme %s (context: %s)", scheme.c_str(),
                          logTargetCfg->getFullContext());
        return nullptr;
    }

    // configure common properties (just this target, not recursively nested)
    if (!configureLogTargetCommon(logTarget, logTargetCfg)) {
        delete logTarget;
        return nullptr;
    }
    return logTarget;
}

ELogFlushPolicy* ELogConfigLoader::loadFlushPolicy(const ELogConfigMapNode* logTargetCfg,
                                                   bool allowNone, bool& result) {
    result = false;
    const ELogConfigValue* cfgValue = logTargetCfg->getValue("flush_policy");
    if (cfgValue == nullptr) {
        // it is ok not to find a flush policy
        // in this case the result is true, but the returned flush policy is null
        result = true;
        return nullptr;
    }

    // NOTE: flush policy could be a flat string or an object
    if (cfgValue->getValueType() == ELogConfigValueType::ELOG_CONFIG_STRING_VALUE) {
        const char* flushPolicyCfg = ((const ELogConfigStringValue*)cfgValue)->getStringValue();
        if (flushPolicyCfg == nullptr || *flushPolicyCfg == 0) {
            ELOG_REPORT_ERROR("Empty flush policy is not allowed at this context: %s",
                              cfgValue->getFullContext());
            result = false;
            return nullptr;
        }

        // unlike previous implementation, it is allowed to specify here a free style expression
        // such as: ((count == 4096) OR (size == 1024) OR (timeoutMillis == 1000))
        // this will be distinguished from normal case by the presence of parenthesis
        if (flushPolicyCfg[0] == '(') {
            return loadFlushPolicyExprStr(flushPolicyCfg, result);
        }

        // otherwise we allow the flush policy properties to be specified at the same level as the
        // log target
        return loadFlushPolicy(logTargetCfg, flushPolicyCfg, allowNone, result);
    }

    if (cfgValue->getValueType() != ELogConfigValueType::ELOG_CONFIG_MAP_VALUE) {
        ELOG_REPORT_ERROR(
            "Invalid configuration value type %s for flush policy, neither string nor map "
            "(context: %s)",
            configValueTypeToString(cfgValue->getValueType()), cfgValue->getFullContext());
        return nullptr;
    }

    // we allow for flush policy to be specified as an object
    const ELogConfigMapNode* flushPolicyCfg = ((const ELogConfigMapValue*)cfgValue)->getMapNode();
    std::string flushPolicyType;
    bool found = false;
    if (!flushPolicyCfg->getStringValue("type", found, flushPolicyType)) {
        ELOG_REPORT_ERROR("Failed to configure flush policy for log target (context: %s)",
                          flushPolicyCfg->getFullContext());
        return nullptr;
    }
    if (!found) {
        ELOG_REPORT_ERROR(
            "Cannot configure flush policy for log target, missing type property (context: %s)",
            flushPolicyCfg->getFullContext());
        return nullptr;
    }

    return loadFlushPolicy(flushPolicyCfg, flushPolicyType.c_str(), allowNone, result);
}

ELogFilter* ELogConfigLoader::loadLogFilter(const ELogConfigMapNode* logTargetCfg, bool& result) {
    result = false;
    const ELogConfigValue* cfgValue = logTargetCfg->getValue("filter");
    if (cfgValue == nullptr) {
        // it is ok not to find a filter
        // in this case the result is true, but the returned filter is null
        result = true;
        return nullptr;
    }

    // NOTE: filter could be a flat string or an object
    if (cfgValue->getValueType() == ELogConfigValueType::ELOG_CONFIG_STRING_VALUE) {
        const char* filterCfg = ((const ELogConfigStringValue*)cfgValue)->getStringValue();
        if (filterCfg == nullptr || *filterCfg == 0) {
            ELOG_REPORT_ERROR("Empty filter value is not allowed at this context: %s",
                              cfgValue->getFullContext());
            result = false;
            return nullptr;
        }

        // unlike previous implementation, it is allowed to specify here a free style expression
        // such as: ((log_source == core.files) OR (tname == main) OR (file LIKE .*cpp))
        // this will be distinguished from normal case by the presence of parenthesis
        if (filterCfg[0] == '(') {
            // TODO: in case of a string we need to parse it and build a filter object
            return loadLogFilterExprStr(filterCfg);
        }

        // otherwise we allow the filter properties to be specified at the same level as the log
        // target
        return loadLogFilter(logTargetCfg, filterCfg, result);
    }

    if (cfgValue->getValueType() != ELogConfigValueType::ELOG_CONFIG_MAP_VALUE) {
        ELOG_REPORT_ERROR(
            "Invalid configuration value type %s for filter, neither string nor map (context: %s)",
            configValueTypeToString(cfgValue->getValueType()), cfgValue->getFullContext());
        return nullptr;
    }

    const ELogConfigMapNode* filterCfg = ((const ELogConfigMapValue*)cfgValue)->getMapNode();
    std::string filterType;
    bool found = false;
    if (!filterCfg->getStringValue("type", found, filterType)) {
        ELOG_REPORT_ERROR("Failed to configure filter for log target (context: %s)",
                          filterCfg->getFullContext());
        return nullptr;
    }
    if (!found) {
        ELOG_REPORT_ERROR(
            "Cannot configure filter for log target, missing type property (context: %s)",
            filterCfg->getFullContext());
        return nullptr;
    }

    return loadLogFilter(filterCfg, filterType.c_str(), result);
}

bool ELogConfigLoader::getLogTargetStringProperty(const ELogConfigMapNode* logTargetCfg,
                                                  const char* scheme, const char* propName,
                                                  std::string& propValue) {
    bool found = false;
    if (!logTargetCfg->getStringValue(propName, found, propValue)) {
        ELOG_REPORT_ERROR("Failed to retrieve '%s' property of %s log target (context: %s)",
                          propName, scheme, logTargetCfg->getFullContext());
        return false;
    }
    if (!found) {
        ELOG_REPORT_ERROR(
            "Invalid %s log target specification, missing required property '%s' (context: %s)",
            scheme, propName, logTargetCfg->getFullContext());
        return false;
    }
    return true;
}

bool ELogConfigLoader::getLogTargetIntProperty(const ELogConfigMapNode* logTargetCfg,
                                               const char* scheme, const char* propName,
                                               int64_t& propValue) {
    bool found = false;
    if (!logTargetCfg->getIntValue(propName, found, propValue)) {
        ELOG_REPORT_ERROR("Failed to retrieve '%s' property of %s log target (context: %s)",
                          propName, scheme, logTargetCfg->getFullContext());
        return false;
    }
    if (!found) {
        ELOG_REPORT_ERROR(
            "Invalid %s log target specification, missing required property '%s' (context: %s)",
            scheme, propName, logTargetCfg->getFullContext());
        return false;
    }
    return true;
}

bool ELogConfigLoader::getLogTargetUInt32Property(const ELogConfigMapNode* logTargetCfg,
                                                  const char* scheme, const char* propName,
                                                  uint32_t& propValue) {
    int64_t intValue = 0;
    if (!getLogTargetIntProperty(logTargetCfg, scheme, propName, intValue)) {
        return false;
    }
    if (intValue > UINT32_MAX) {
        ELOG_REPORT_ERROR("Invalid '%s' property '%s' value %" PRId64
                          ", exceeding allowed maximum %u (context: %s)",
                          scheme, propName, intValue, (unsigned)UINT32_MAX,
                          logTargetCfg->getFullContext());
        return false;
    }
    propValue = (uint32_t)intValue;
    return true;
}

bool ELogConfigLoader::getLogTargetBoolProperty(const ELogConfigMapNode* logTargetCfg,
                                                const char* scheme, const char* propName,
                                                bool& propValue) {
    bool found = false;
    if (!logTargetCfg->getBoolValue(propName, found, propValue)) {
        ELOG_REPORT_ERROR("Failed to retrieve '%s' property of %s log target (context: %s)",
                          propName, scheme, logTargetCfg->getFullContext());
        return false;
    }
    if (!found) {
        ELOG_REPORT_ERROR(
            "Invalid %s log target specification, missing required property '%s' (context: %s)",
            scheme, propName, logTargetCfg->getFullContext());
        return false;
    }
    return true;
}

bool ELogConfigLoader::getLogTargetTimeoutProperty(const ELogConfigMapNode* logTargetCfg,
                                                   const char* scheme, const char* propName,
                                                   uint64_t& propValue, ELogTimeUnits targetUnits) {
    bool found = false;
    std::string value;
    if (!logTargetCfg->getStringValue(propName, found, value)) {
        ELOG_REPORT_ERROR("Failed to retrieve '%s' property of %s log target (context: %s)",
                          propName, scheme, logTargetCfg->getFullContext());
        return false;
    }
    if (!found) {
        ELOG_REPORT_ERROR(
            "Invalid %s log target specification, missing required property '%s' (context: %s)",
            scheme, propName, logTargetCfg->getFullContext());
        return false;
    }
    ELogTimeUnits origUnits = ELogTimeUnits::TU_NONE;
    return parseTimeValueProp(propName, "", value, propValue, origUnits, targetUnits);
}

bool ELogConfigLoader::getLogTargetSizeProperty(const ELogConfigMapNode* logTargetCfg,
                                                const char* scheme, const char* propName,
                                                uint64_t& propValue, ELogSizeUnits targetUnits) {
    bool found = false;
    std::string value;
    if (!logTargetCfg->getStringValue(propName, found, value)) {
        ELOG_REPORT_ERROR("Failed to retrieve '%s' property of %s log target (context: %s)",
                          propName, scheme, logTargetCfg->getFullContext());
        return false;
    }
    if (!found) {
        ELOG_REPORT_ERROR(
            "Invalid %s log target specification, missing required property '%s' (context: %s)",
            scheme, propName, logTargetCfg->getFullContext());
        return false;
    }
    return parseSizeProp(propName, "", value, propValue, targetUnits);
}

bool ELogConfigLoader::getOptionalLogTargetStringProperty(const ELogConfigMapNode* logTargetCfg,
                                                          const char* scheme, const char* propName,
                                                          std::string& propValue,
                                                          bool* found /* = nullptr */) {
    bool foundLocal = false;
    if (!logTargetCfg->getStringValue(propName, foundLocal, propValue)) {
        ELOG_REPORT_ERROR("Failed to retrieve '%s' property of %s log target (context: %s)",
                          propName, scheme, logTargetCfg->getFullContext());
        return false;
    }
    if (found != nullptr) {
        *found = foundLocal;
    }
    return true;
}

bool ELogConfigLoader::getOptionalLogTargetIntProperty(const ELogConfigMapNode* logTargetCfg,
                                                       const char* scheme, const char* propName,
                                                       int64_t& propValue,
                                                       bool* found /* = nullptr */) {
    bool foundLocal = false;
    if (!logTargetCfg->getIntValue(propName, foundLocal, propValue)) {
        ELOG_REPORT_ERROR("Failed to retrieve '%s' property of %s log target (context: %s)",
                          propName, scheme, logTargetCfg->getFullContext());
        return false;
    }
    if (found != nullptr) {
        *found = foundLocal;
    }
    return true;
}

bool ELogConfigLoader::getOptionalLogTargetInt32Property(const ELogConfigMapNode* logTargetCfg,
                                                         const char* scheme, const char* propName,
                                                         int32_t& propValue,
                                                         bool* found /* = nullptr */) {
    int64_t intValue = 0;
    bool foundLocal = false;
    bool res =
        getOptionalLogTargetIntProperty(logTargetCfg, scheme, propName, intValue, &foundLocal);
    if (!res) {
        return false;
    }
    if (foundLocal) {
        if (intValue > INT32_MAX || intValue < INT32_MIN) {
            ELOG_REPORT_ERROR("Invalid '%s' property '%s' value %" PRId64
                              ", exceeding allowed range [%d, %d] (context: %s)",
                              scheme, propName, intValue, (int)INT32_MIN, (int)INT32_MAX,
                              logTargetCfg->getFullContext());
            return false;
        }
        propValue = (int32_t)intValue;
    }
    if (found != nullptr) {
        *found = foundLocal;
    }
    return true;
}

bool ELogConfigLoader::getOptionalLogTargetUIntProperty(const ELogConfigMapNode* logTargetCfg,
                                                        const char* scheme, const char* propName,
                                                        uint64_t& propValue,
                                                        bool* found /* = nullptr */) {
    int64_t intValue = 0;
    bool foundLocal = false;
    bool res =
        getOptionalLogTargetIntProperty(logTargetCfg, scheme, propName, intValue, &foundLocal);
    if (!res) {
        return false;
    }
    if (foundLocal) {
        if (intValue < 0) {
            ELOG_REPORT_ERROR("Invalid '%s' property '%s' value %" PRId64
                              ", expected non-negative number (context: %s)",
                              scheme, propName, intValue, logTargetCfg->getFullContext());
            return false;
        }
        propValue = (uint64_t)intValue;
    }
    if (found != nullptr) {
        *found = foundLocal;
    }
    return true;
}

bool ELogConfigLoader::getOptionalLogTargetUInt32Property(const ELogConfigMapNode* logTargetCfg,
                                                          const char* scheme, const char* propName,
                                                          uint32_t& propValue,
                                                          bool* found /* = nullptr */) {
    uint64_t intValue = 0;
    bool foundLocal = false;
    bool res =
        getOptionalLogTargetUIntProperty(logTargetCfg, scheme, propName, intValue, &foundLocal);
    if (!res) {
        return false;
    }
    if (foundLocal) {
        if (intValue > UINT32_MAX) {
            ELOG_REPORT_ERROR("Invalid '%s' property '%s' value %" PRId64
                              ", exceeding allowed maximum %u (context: %s)",
                              scheme, propName, intValue, (unsigned)UINT32_MAX,
                              logTargetCfg->getFullContext());
            return false;
        }
        propValue = (uint32_t)intValue;
    }
    if (found != nullptr) {
        *found = foundLocal;
    }
    return true;
}

bool ELogConfigLoader::getOptionalLogTargetBoolProperty(const ELogConfigMapNode* logTargetCfg,
                                                        const char* scheme, const char* propName,
                                                        bool& propValue,
                                                        bool* found /* = nullptr */) {
    bool foundLocal = false;
    if (!logTargetCfg->getBoolValue(propName, foundLocal, propValue)) {
        ELOG_REPORT_ERROR("Failed to retrieve '%s' property of %s log target (context: %s)",
                          propName, scheme, logTargetCfg->getFullContext());
        return false;
    }
    if (found != nullptr) {
        *found = foundLocal;
    }
    return true;
}

bool ELogConfigLoader::getOptionalLogTargetTimeoutProperty(const ELogConfigMapNode* logTargetCfg,
                                                           const char* scheme, const char* propName,
                                                           uint64_t& propValue,
                                                           ELogTimeUnits targetUnits,
                                                           bool* found /* = nullptr */) {
    bool foundLocal = false;
    std::string value;
    if (!logTargetCfg->getStringValue(propName, foundLocal, value)) {
        ELOG_REPORT_ERROR("Failed to retrieve '%s' property of %s log target (context: %s)",
                          propName, scheme, logTargetCfg->getFullContext());
        return false;
    }
    if (found != nullptr) {
        *found = foundLocal;
    }
    if (!foundLocal) {
        return true;
    }
    ELogTimeUnits origUnits = ELogTimeUnits::TU_NONE;
    return parseTimeValueProp(propName, "", value, propValue, origUnits, targetUnits);
}

bool ELogConfigLoader::getOptionalLogTargetSizeProperty(const ELogConfigMapNode* logTargetCfg,
                                                        const char* scheme, const char* propName,
                                                        uint64_t& propValue,
                                                        ELogSizeUnits targetUnits,
                                                        bool* found /* = nullptr */) {
    bool foundLocal = false;
    std::string value;
    if (!logTargetCfg->getStringValue(propName, foundLocal, value)) {
        ELOG_REPORT_ERROR("Failed to retrieve '%s' property of %s log target (context: %s)",
                          propName, scheme, logTargetCfg->getFullContext());
        return false;
    }
    if (found != nullptr) {
        *found = foundLocal;
    }
    if (!foundLocal) {
        return true;
    }
    return parseSizeProp(propName, "", value, propValue, targetUnits);
}

ELogFlushPolicy* ELogConfigLoader::loadFlushPolicyExprStr(const char* flushPolicyExpr,
                                                          bool& result) {
    ELogExpression* expr = ELogExpressionParser::parseExpressionString(flushPolicyExpr);
    if (expr == nullptr) {
        ELOG_REPORT_ERROR("Failed to parse flush policy expression: %s", flushPolicyExpr);
        return nullptr;
    }
    ELogFlushPolicy* flushPolicy = loadFlushPolicyExpr(expr);
    delete expr;
    result = true;
    return flushPolicy;
}

ELogFlushPolicy* ELogConfigLoader::loadFlushPolicyExpr(const ELogExpression* expr) {
    // TODO: use coherent terminology, either compound of composite
    ELogFlushPolicy* flushPolicy = nullptr;
    if (expr->m_type == ELogExpressionType::ET_AND_EXPR) {
        flushPolicy = new (std::nothrow) ELogAndFlushPolicy();
    } else if (expr->m_type == ELogExpressionType::ET_OR_EXPR) {
        flushPolicy = new (std::nothrow) ELogOrFlushPolicy();
    } else if (expr->m_type == ELogExpressionType::ET_NOT_EXPR) {
        flushPolicy = new (std::nothrow) ELogNotFlushPolicy();
    } else if (expr->m_type == ELogExpressionType::ET_CHAIN_EXPR) {
        flushPolicy = new (std::nothrow) ELogChainedFlushPolicy();
    } else if (expr->m_type == ELogExpressionType::ET_FUNC_EXPR) {
        // the function name should be able to load a composite flush policy by name
        const ELogFunctionExpression* funcExpr = (const ELogFunctionExpression*)expr;
        flushPolicy = constructFlushPolicy(funcExpr->m_functionName.c_str());
        if (flushPolicy == nullptr) {
            ELOG_REPORT_ERROR("Failed to construct flush policy by name '%s'",
                              funcExpr->m_functionName.c_str());
            return nullptr;
        }
    } else if (expr->m_type == ELogExpressionType::ET_NAME_EXPR) {
        const ELogNameExpression* nameExpr = (const ELogNameExpression*)expr;
        flushPolicy = constructFlushPolicy(nameExpr->m_name.c_str());
        if (flushPolicy == nullptr) {
            ELOG_REPORT_ERROR("Failed to load flush policy by name '%s", nameExpr->m_name.c_str());
            return nullptr;
        }
    } else {
        assert(expr->m_type == ELogExpressionType::ET_OP_EXPR);
        // LHS is always the flush policy name
        // RHS is the value (size/count/time-millis), always an integer
        // OP is always "=="
        const ELogOpExpression* opExpr = (const ELogOpExpression*)expr;
        if (opExpr->m_op.compare("==") != 0 && opExpr->m_op.compare(":") != 0) {
            ELOG_REPORT_ERROR(
                "Invalid flush policy operation '%s', only equals (==), or assign (:) operator is "
                "allowed in this context",
                opExpr->m_op.c_str());
            return nullptr;
        }
        uint64_t value = 0;
        if (!parseIntProp("", "", opExpr->m_rhs, value)) {
            ELOG_REPORT_ERROR("Invalid flush policy argument '%s', expected integer type",
                              opExpr->m_rhs.c_str());
            return nullptr;
        }
        flushPolicy = constructFlushPolicy(opExpr->m_lhs.c_str());
        if (flushPolicy == nullptr) {
            ELOG_REPORT_ERROR("Failed to construct flush policy by name '%s",
                              opExpr->m_lhs.c_str());
            return nullptr;
        }
    }

    if (flushPolicy == nullptr) {
        ELOG_REPORT_ERROR("Failed to allocate flush policy, out of memory");
        return nullptr;
    }
    if (!flushPolicy->loadExpr(expr)) {
        ELOG_REPORT_ERROR("Failed to load compound flush policy from expression");
        delete flushPolicy;
        flushPolicy = nullptr;
    }
    return flushPolicy;
}

ELogFlushPolicy* ELogConfigLoader::loadFlushPolicy(const ELogConfigMapNode* flushPolicyCfg,
                                                   const char* flushPolicyType, bool allowNone,
                                                   bool& result) {
    if (strcmp(flushPolicyType, "none") == 0) {
        // special case, let target decide by itself what happens when no flush policy is set
        if (!allowNone) {
            ELOG_REPORT_ERROR("None flush policy is not allowed in this context (%s)",
                              flushPolicyCfg->getFullContext());
        } else {
            result = true;
        }
        return nullptr;
    }

    ELogFlushPolicy* flushPolicy = constructFlushPolicy(flushPolicyType);
    if (flushPolicy == nullptr) {
        ELOG_REPORT_ERROR("Failed to create flush policy by type %s (context: %s)", flushPolicyType,
                          flushPolicyCfg->getFullContext());
        return nullptr;
    }

    if (!flushPolicy->load(flushPolicyCfg)) {
        ELOG_REPORT_ERROR("Failed to load flush policy %s by configuration object (context: %s)",
                          flushPolicyType, flushPolicyCfg->getFullContext());
        delete flushPolicy;
        flushPolicy = nullptr;
    } else {
        result = true;
    }
    return flushPolicy;
}

ELogFilter* ELogConfigLoader::loadLogFilterExprStr(const char* filterExpr) {
    ELogExpression* expr = ELogExpressionParser::parseExpressionString(filterExpr);
    if (expr == nullptr) {
        ELOG_REPORT_ERROR("Failed to parse filter expression: %s", filterExpr);
        return nullptr;
    }
    ELogFilter* filter = loadLogFilterExpr(expr);
    delete expr;
    return filter;
}

ELogFilter* ELogConfigLoader::loadLogFilterExpr(ELogExpression* expr) {
    if (expr->m_type == ELogExpressionType::ET_AND_EXPR ||
        expr->m_type == ELogExpressionType::ET_OR_EXPR) {
        // TODO: use coherent terminology, either compound of composite
        ELogCompositeExpression* andExpr = (ELogCompositeExpression*)expr;
        ELogCompoundLogFilter* filter = nullptr;
        if (expr->m_type == ELogExpressionType::ET_AND_EXPR) {
            filter = new (std::nothrow) ELogAndLogFilter();
        } else {
            filter = new (std::nothrow) ELogOrLogFilter();
        }
        if (filter == nullptr) {
            ELOG_REPORT_ERROR("Failed to allocate filter, out of memory");
            return nullptr;
        }
        for (ELogExpression* subExpr : andExpr->m_expressions) {
            ELogFilter* subFilter = loadLogFilterExpr(subExpr);
            if (subFilter == nullptr) {
                ELOG_REPORT_ERROR("Failed to load sub-filter from expression");
                delete filter;
                return nullptr;
            }
            filter->addFilter(subFilter);
        }
        return filter;
    } else if (expr->m_type == ELogExpressionType::ET_NOT_EXPR) {
        ELogNotExpression* notExpr = (ELogNotExpression*)expr;
        ELogFilter* subFilter = loadLogFilterExpr(notExpr->m_expression);
        if (subFilter == nullptr) {
            ELOG_REPORT_ERROR("Failed to load sub-filter for NOT filter");
            return nullptr;
        }
        ELogNotFilter* notFilter = new (std::nothrow) ELogNotFilter(subFilter);
        if (notFilter == nullptr) {
            ELOG_REPORT_ERROR("Failed to allocate filter, out of memory");
            delete subFilter;
            return nullptr;
        }
        return notFilter;
    } else {
        assert(expr->m_type == ELogExpressionType::ET_OP_EXPR);
        // LHS is always the filter name
        // RHS is the value (int/time-str/str/log-level)
        // OP is always "=="
        ELogOpExpression* opExpr = (ELogOpExpression*)expr;
        if (opExpr->m_op.compare("==") != 0) {
            ELOG_REPORT_ERROR("Invalid filter operation '%s', only equals operator supported",
                              opExpr->m_op.c_str());
            return nullptr;
        }
        ELogFilter* filter = constructFilter(opExpr->m_lhs.c_str());
        if (filter == nullptr) {
            ELOG_REPORT_ERROR("Failed to load filter by name '%s", opExpr->m_lhs.c_str());
            return nullptr;
        }

        // now have the filter load itself from the parsed expression
        if (!filter->loadExpr(opExpr)) {
            ELOG_REPORT_ERROR("Failed to load filter from expression");
            delete filter;
            return nullptr;
        }
        return filter;
    }
}

ELogFilter* ELogConfigLoader::loadLogFilter(const ELogConfigMapNode* filterCfg,
                                            const char* filterType, bool& result) {
    ELogFilter* filter = constructFilter(filterType);
    if (filter == nullptr) {
        ELOG_REPORT_ERROR("Failed to create filter by type %s (context: %s)", filterType,
                          filterCfg->getFullContext());
        return nullptr;
    }

    if (!filter->load(filterCfg)) {
        ELOG_REPORT_ERROR("Failed to load filter %s by configuration object (context: %s)",
                          filterType, filterCfg->getFullContext());
        delete filter;
        filter = nullptr;
    } else {
        result = true;
    }
    return filter;
}

ELogFormatter* ELogConfigLoader::loadLogFormatter(const char* logFormat) {
    ELogFormatter* logFormatter = nullptr;
    std::string logFormatStr = logFormat;
    // parse optional type
    std::string::size_type colonPos = logFormatStr.find(':');
    if (colonPos != std::string::npos && logFormatStr[0] != '$') {
        std::string type = logFormatStr.substr(0, colonPos);
        logFormatter = constructLogFormatter(type.c_str(), false);
        if (logFormatter == nullptr) {
            // NOTE: we do not have the ability to tell whether this is a real error, since user
            // string may contain a colon, so we issue a warning and continue
            ELOG_REPORT_WARN("Invalid log formatter type '%s', continuing as string formatter",
                             type.c_str());
            // use entire string as log format
        } else {
            // skip to actual log format
            logFormatStr = logFormatStr.substr(colonPos + 1);
        }
    }

    // create default formatter if needed
    if (logFormatter == nullptr) {
        logFormatter = new (std::nothrow) ELogFormatter();
        if (logFormatter == nullptr) {
            ELOG_REPORT_ERROR("Failed to allocate log formatter for log target, out of memory");
            return nullptr;
        }
    }

    // initialize the formatter (parse field selectors)
    if (!logFormatter->initialize(logFormatStr.c_str())) {
        ELOG_REPORT_ERROR("Invalid log format '%s' specified in log target", logFormat);
        delete logFormatter;
        return nullptr;
    }

    return logFormatter;
}

bool ELogConfigLoader::configureLogTargetCommon(ELogTarget* logTarget,
                                                const ELogConfigMapNode* logTargetCfg) {
    // apply target name if any
    if (!applyTargetName(logTarget, logTargetCfg)) {
        return false;
    }

    // apply target log level if any
    if (!applyTargetLogLevel(logTarget, logTargetCfg)) {
        return false;
    }

    // apply log format if any
    if (!applyTargetLogFormat(logTarget, logTargetCfg)) {
        return false;
    }

    // apply flush policy if any
    if (!applyTargetFlushPolicy(logTarget, logTargetCfg)) {
        return false;
    }

    // apply filter if any
    if (!applyTargetFilter(logTarget, logTargetCfg)) {
        return false;
    }
    return true;
}

bool ELogConfigLoader::applyTargetName(ELogTarget* logTarget,
                                       const ELogConfigMapNode* logTargetCfg) {
    std::string name;
    bool found = false;
    if (!logTargetCfg->getStringValue("name", found, name)) {
        ELOG_REPORT_ERROR("Failed to set log target name (context: %s)",
                          logTargetCfg->getFullContext());
        return false;
    }

    if (found) {
        logTarget->setName(name.c_str());
    }
    return true;
}

bool ELogConfigLoader::applyTargetLogLevel(ELogTarget* logTarget,
                                           const ELogConfigMapNode* logTargetCfg) {
    std::string logLevelStr;
    bool found = false;
    if (!logTargetCfg->getStringValue("log_level", found, logLevelStr)) {
        ELOG_REPORT_ERROR("Failed to set log level for target (context: %s)",
                          logTargetCfg->getFullContext());
        return false;
    }

    if (found) {
        ELogLevel logLevel = ELEVEL_INFO;
        if (!elogLevelFromStr(logLevelStr.c_str(), logLevel)) {
            ELOG_REPORT_ERROR("Invalid log level '%s' specified in log target (context: %s)",
                              logLevelStr.c_str(), logTargetCfg->getFullContext());
            return false;
        }
        logTarget->setLogLevel(logLevel);
    }
    return true;
}

bool ELogConfigLoader::applyTargetLogFormat(ELogTarget* logTarget,
                                            const ELogConfigMapNode* logTargetCfg) {
    std::string logFormat;
    bool found = false;
    if (!logTargetCfg->getStringValue("log_format", found, logFormat)) {
        ELOG_REPORT_ERROR("Failed to set log format for log target (context: %s)",
                          logTargetCfg->getFullContext());
        return false;
    }

    if (found) {
        ELogFormatter* logFormatter = loadLogFormatter(logFormat.c_str());
        if (logFormatter == nullptr) {
            ELOG_REPORT_ERROR("Failed to load log formatter from string: %s (context: %s)",
                              logFormat.c_str(), logTargetCfg->getFullContext());
            return false;
        }
        logTarget->setLogFormatter(logFormatter);
    }
    return true;
}

bool ELogConfigLoader::applyTargetFlushPolicy(ELogTarget* logTarget,
                                              const ELogConfigMapNode* logTargetCfg) {
    bool result = false;
    ELogFlushPolicy* flushPolicy = loadFlushPolicy(logTargetCfg, true, result);
    if (!result) {
        return false;
    }
    if (flushPolicy != nullptr) {
        // active policies require a log target
        if (flushPolicy->isActive()) {
            flushPolicy->setLogTarget(logTarget);
        }

        // that's it
        logTarget->setFlushPolicy(flushPolicy);
    }
    return true;
}

bool ELogConfigLoader::applyTargetFilter(ELogTarget* logTarget,
                                         const ELogConfigMapNode* logTargetCfg) {
    bool result = false;
    ELogFilter* filter = loadLogFilter(logTargetCfg, result);
    if (!result) {
        return false;
    }
    if (filter != nullptr) {
        logTarget->setLogFilter(filter);
    }
    return true;
}

}  // namespace elog