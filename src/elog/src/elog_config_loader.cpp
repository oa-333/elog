#include "elog_config_loader.h"

#include <cassert>
#include <fstream>

#include "elog_common.h"
#include "elog_deferred_target.h"
#include "elog_error.h"
#include "elog_expression_parser.h"
#include "elog_filter.h"
#include "elog_formatter.h"
#include "elog_quantum_target.h"
#include "elog_queued_target.h"
#include "elog_schema_manager.h"

namespace elog {

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
    uint32_t openBraceCount = 0;
    uint32_t closeBraceCount = 0;
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
        openBraceCount += std::count(line.begin(), line.end(), '{');
        closeBraceCount += std::count(line.begin(), line.end(), '}');

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

ELogTarget* ELogConfigLoader::loadLogTarget(const std::string& logTargetCfg,
                                            const ELogTargetNestedSpec& logTargetNestedSpec,
                                            ELogTargetSpecStyle specStyle) {
    ELogSchemaHandler* schemaHandler =
        ELogSchemaManager::getSchemaHandler(logTargetNestedSpec.m_spec.m_scheme.c_str());
    if (schemaHandler != nullptr) {
        ELogTarget* logTarget = schemaHandler->loadTarget(logTargetCfg, logTargetNestedSpec);
        if (logTarget == nullptr) {
            ELOG_REPORT_ERROR("Failed to load target for scheme %s: %s",
                              logTargetNestedSpec.m_spec.m_scheme.c_str(), logTargetCfg.c_str());
            return nullptr;
        }

        // in case of nested style, there no need to apply compound target, the schema handler
        // already loads it nested (this is actually done in recursive manner, schema handler calls
        // configureLogTarget for each sub target, which in turn activates the schema handler again)
        if (specStyle == ELogTargetSpecStyle::ELOG_STYLE_URL) {
            // apply compound target
            bool errorOccurred = false;
            ELogTarget* compoundTarget = applyCompoundTarget(
                logTarget, logTargetCfg, logTargetNestedSpec.m_spec, errorOccurred);
            if (errorOccurred) {
                ELOG_REPORT_ERROR("Failed to apply compound log target specification");
                delete logTarget;
                return nullptr;
            }
            if (compoundTarget != nullptr) {
                logTarget = compoundTarget;
            }
        }

        // configure common properties (just this target, not recursively nested)
        if (!configureLogTargetCommon(logTarget, logTargetCfg, logTargetNestedSpec)) {
            delete logTarget;
            return nullptr;
        }
        return logTarget;
    }

    ELOG_REPORT_ERROR("Invalid log target specification, unrecognized scheme %s: %s",
                      logTargetNestedSpec.m_spec.m_scheme.c_str(), logTargetCfg.c_str());
    return nullptr;
}

ELogFlushPolicy* ELogConfigLoader::loadFlushPolicy(const std::string& logTargetCfg,
                                                   const ELogTargetNestedSpec& logTargetNestedSpec,
                                                   bool allowNone, bool& result) {
    result = true;
    ELogPropertyMap::const_iterator itr = logTargetNestedSpec.m_spec.m_props.find("flush_policy");
    if (itr == logTargetNestedSpec.m_spec.m_props.end()) {
        // it is ok not to find a flush policy, in this ok result is true but flush policy is null
        return nullptr;
    }

    const std::string& flushPolicyCfg = itr->second;
    if (flushPolicyCfg.compare("none") == 0) {
        // special case, let target decide by itself what happens when no flush policy is set
        if (!allowNone) {
            ELOG_REPORT_ERROR("None flush policy is not allowed in this context");
            result = false;
        }
        return nullptr;
    }

    ELogFlushPolicy* flushPolicy = constructFlushPolicy(flushPolicyCfg.c_str());
    if (flushPolicy == nullptr) {
        ELOG_REPORT_ERROR("Failed to create flush policy by type %s: %s", flushPolicyCfg.c_str(),
                          logTargetCfg.c_str());
        result = false;
        return nullptr;
    }

    if (!flushPolicy->load(logTargetCfg, logTargetNestedSpec)) {
        ELOG_REPORT_ERROR("Failed to load flush policy by properties %s: %s",
                          flushPolicyCfg.c_str(), logTargetCfg.c_str());
        result = false;
        delete flushPolicy;
        flushPolicy = nullptr;
    }
    return flushPolicy;
}

ELogFilter* ELogConfigLoader::loadLogFilter(const std::string& logTargetCfg,
                                            const ELogTargetNestedSpec& logTargetNestedSpec,
                                            bool& result) {
    result = true;
    ELogFilter* filter = nullptr;
    ELogPropertyMap::const_iterator itr = logTargetNestedSpec.m_spec.m_props.find("filter");
    if (itr != logTargetNestedSpec.m_spec.m_props.end()) {
        const std::string& filterCfg = itr->second;
        filter = constructFilter(filterCfg.c_str());
        if (filter == nullptr) {
            ELOG_REPORT_ERROR("Failed to create filter by type %s: %s", filterCfg.c_str(),
                              logTargetCfg.c_str());
            result = false;
        } else if (!filter->load(logTargetCfg, logTargetNestedSpec)) {
            ELOG_REPORT_ERROR("Failed to load filter by properties %s: %s", filterCfg.c_str(),
                              logTargetCfg.c_str());
            delete filter;
            filter = nullptr;
            result = false;
        }
    }
    return filter;
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
            return loadFlushPolicyExprStr(flushPolicyCfg);
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
    const ELogConfigMapNode* flushPolicyCfg = ((ELogConfigMapValue*)cfgValue)->getMapNode();
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

    const ELogConfigMapNode* filterCfg = ((ELogConfigMapValue*)cfgValue)->getMapNode();
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

ELogFlushPolicy* ELogConfigLoader::loadFlushPolicyExprStr(const char* flushPolicyExpr) {
    ELogExpression* expr = ELogExpressionParser::parseExpressionString(flushPolicyExpr);
    if (expr == nullptr) {
        ELOG_REPORT_ERROR("Failed to parse flush policy expression: %s", flushPolicyExpr);
        return nullptr;
    }
    ELogFlushPolicy* flushPolicy = loadFlushPolicyExpr(expr);
    delete expr;
    return flushPolicy;
}

ELogFlushPolicy* ELogConfigLoader::loadFlushPolicyExpr(ELogExpression* expr) {
    if (expr->m_type == ELogExpressionType::ET_AND_EXPR ||
        expr->m_type == ELogExpressionType::ET_OR_EXPR) {
        // TODO: use coherent terminology, either compound of composite
        ELogCompositeExpression* andExpr = (ELogCompositeExpression*)expr;
        ELogCompoundFlushPolicy* flushPolicy = nullptr;
        if (expr->m_type == ELogExpressionType::ET_AND_EXPR) {
            flushPolicy = new (std::nothrow) ELogAndFlushPolicy();
        } else {
            flushPolicy = new (std::nothrow) ELogOrFlushPolicy();
        }
        if (flushPolicy == nullptr) {
            ELOG_REPORT_ERROR("Failed to allocate flush policy, out of memory");
            return nullptr;
        }
        for (ELogExpression* subExpr : andExpr->m_expressions) {
            ELogFlushPolicy* subFlushPolicy = loadFlushPolicyExpr(subExpr);
            if (subFlushPolicy == nullptr) {
                ELOG_REPORT_ERROR("Failed to load sub-flush policy from expression");
                delete flushPolicy;
                return nullptr;
            }
            flushPolicy->addFlushPolicy(subFlushPolicy);
        }
        return flushPolicy;
    } else if (expr->m_type == ELogExpressionType::ET_NOT_EXPR) {
        ELogNotExpression* notExpr = (ELogNotExpression*)expr;
        ELogFlushPolicy* subFlushPolicy = loadFlushPolicyExpr(notExpr->m_expression);
        if (subFlushPolicy == nullptr) {
            ELOG_REPORT_ERROR("Failed to load sub-flush policy for NOT flush policy");
            return nullptr;
        }
        ELogNotFlushPolicy* notFlushPolicy = new (std::nothrow) ELogNotFlushPolicy(subFlushPolicy);
        if (notFlushPolicy == nullptr) {
            ELOG_REPORT_ERROR("Failed to allocate flush policy, out of memory");
            delete subFlushPolicy;
            return nullptr;
        }
        return notFlushPolicy;
    } else {
        assert(expr->m_type == ELogExpressionType::ET_OP_EXPR);
        // LHS is always the flush policy name
        // RHS is the value (size/count/time-millis), always an integer
        // OP is always "=="
        ELogOpExpression* opExpr = (ELogOpExpression*)expr;
        if (opExpr->m_op.compare("==") != 0) {
            ELOG_REPORT_ERROR("Invalid flush policy operation '%s', only equals operator supported",
                              opExpr->m_op.c_str());
            return nullptr;
        }
        uint64_t value = 0;
        if (!parseIntProp("", "", opExpr->m_rhs, value)) {
            ELOG_REPORT_ERROR("Invalid flush policy argument '%s', expected integer type",
                              opExpr->m_rhs.c_str());
            return nullptr;
        }
        ELogFlushPolicy* flushPolicy = constructFlushPolicy(opExpr->m_lhs.c_str());
        if (flushPolicy == nullptr) {
            ELOG_REPORT_ERROR("Failed to load flush policy by name '%s", opExpr->m_lhs.c_str());
            return nullptr;
        }

        // now have the flush policy load itself from the parsed expression
        if (!flushPolicy->load(opExpr)) {
            ELOG_REPORT_ERROR("Failed to load flush policy from expression");
            delete flushPolicy;
            return nullptr;
        }
        return flushPolicy;
    }
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
            ELOG_REPORT_ERROR("Failed to allocate flush policy, out of memory");
            delete subFilter;
            return nullptr;
        }
        return notFilter;
    } else {
        assert(expr->m_type == ELogExpressionType::ET_OP_EXPR);
        // LHS is always the flush policy name
        // RHS is the value (size/count/time-millis), always an integer
        // OP is always "=="
        ELogOpExpression* opExpr = (ELogOpExpression*)expr;
        if (opExpr->m_op.compare("==") != 0) {
            ELOG_REPORT_ERROR("Invalid flush policy operation '%s', only equals operator supported",
                              opExpr->m_op.c_str());
            return nullptr;
        }
        uint64_t value = 0;
        if (!parseIntProp("", "", opExpr->m_rhs, value)) {
            ELOG_REPORT_ERROR("Invalid flush policy argument '%s', expected integer type",
                              opExpr->m_rhs.c_str());
            return nullptr;
        }
        ELogFilter* filter = constructFilter(opExpr->m_lhs.c_str());
        if (filter == nullptr) {
            ELOG_REPORT_ERROR("Failed to load filter by name '%s", opExpr->m_lhs.c_str());
            return nullptr;
        }

        // now have the filter load itself from the parsed expression
        if (!filter->load(opExpr)) {
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

bool ELogConfigLoader::configureLogTargetCommon(ELogTarget* logTarget,
                                                const std::string& logTargetCfg,
                                                const ELogTargetNestedSpec& logTargetSpec) {
    // apply target name if any
    applyTargetName(logTarget, logTargetSpec.m_spec);

    // apply target log level if any
    if (!applyTargetLogLevel(logTarget, logTargetCfg, logTargetSpec.m_spec)) {
        return false;
    }

    // apply log format if any
    if (!applyTargetLogFormat(logTarget, logTargetCfg, logTargetSpec.m_spec)) {
        return false;
    }

    // apply flush policy if any
    if (!applyTargetFlushPolicy(logTarget, logTargetCfg, logTargetSpec)) {
        return false;
    }

    // apply filter if any
    if (!applyTargetFilter(logTarget, logTargetCfg, logTargetSpec)) {
        return false;
    }
    return true;
}

void ELogConfigLoader::applyTargetName(ELogTarget* logTarget, const ELogTargetSpec& logTargetSpec) {
    ELogPropertyMap::const_iterator itr = logTargetSpec.m_props.find("name");
    if (itr != logTargetSpec.m_props.end()) {
        logTarget->setName(itr->second.c_str());
    }
}

bool ELogConfigLoader::applyTargetLogLevel(ELogTarget* logTarget, const std::string& logTargetCfg,
                                           const ELogTargetSpec& logTargetSpec) {
    ELogPropertyMap::const_iterator itr = logTargetSpec.m_props.find("log_level");
    if (itr != logTargetSpec.m_props.end()) {
        ELogLevel logLevel = ELEVEL_INFO;
        if (!elogLevelFromStr(itr->second.c_str(), logLevel)) {
            ELOG_REPORT_ERROR("Invalid log level '%s' specified in log target: %s",
                              itr->second.c_str(), logTargetCfg.c_str());
            return false;
        }
        logTarget->setLogLevel(logLevel);
    }
    return true;
}

bool ELogConfigLoader::applyTargetLogFormat(ELogTarget* logTarget, const std::string& logTargetCfg,
                                            const ELogTargetSpec& logTargetSpec) {
    ELogPropertyMap::const_iterator itr = logTargetSpec.m_props.find("log_format");
    if (itr != logTargetSpec.m_props.end()) {
        ELogFormatter* logFormatter = new (std::nothrow) ELogFormatter();
        if (!logFormatter->initialize(itr->second.c_str())) {
            ELOG_REPORT_ERROR("Invalid log format '%s' specified in log target: %s",
                              itr->second.c_str(), logTargetCfg.c_str());
            delete logFormatter;
            return false;
        }
        logTarget->setLogFormatter(logFormatter);
    }
    return true;
}

bool ELogConfigLoader::applyTargetFlushPolicy(ELogTarget* logTarget,
                                              const std::string& logTargetCfg,
                                              const ELogTargetNestedSpec& logTargetSpec) {
    bool result = false;
    ELogFlushPolicy* flushPolicy = loadFlushPolicy(logTargetCfg, logTargetSpec, true, result);
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
    /*ELogPropertyMap::const_iterator itr = logTargetSpec.m_spec.m_props.find("flush_policy");
    if (itr != logTargetSpec.m_spec.m_props.end()) {
        const std::string& flushPolicyCfg = itr->second;
        if (flushPolicyCfg.compare("none") == 0) {
            // special case, let target decide by itself what happens when no flush policy is set
            return true;
        }
        ELogFlushPolicy* flushPolicy = constructFlushPolicy(flushPolicyCfg.c_str());
        if (flushPolicy == nullptr) {
            ELOG_REPORT_ERROR("Failed to create flush policy by type %s: %s",
    flushPolicyCfg.c_str(), logTargetCfg.c_str()); return false;
        }
        if (!flushPolicy->load(logTargetCfg, logTargetSpec)) {
            ELOG_REPORT_ERROR("Failed to load flush policy by properties %s: %s",
    flushPolicyCfg.c_str(), logTargetCfg.c_str()); delete flushPolicy; return false;
        }

        // active policies require a log target
        if (flushPolicy->isActive()) {
            flushPolicy->setLogTarget(logTarget);
        }

        // that's it
        logTarget->setFlushPolicy(flushPolicy);*/

    /*if (flushPolicyCfg.compare("immediate") == 0) {
        flushPolicy = new (std::nothrow) ELogImmediateFlushPolicy();
    } else if (flushPolicyCfg.compare("never") == 0) {
        flushPolicy = new (std::nothrow) ELogNeverFlushPolicy();
    } else if (flushPolicyCfg.compare("count") == 0) {
        ELogPropertyMap::const_iterator itr2 = logTargetSpec.m_props.find("flush_count");
        if (itr2 == logTargetSpec.m_props.end()) {
            ELOG_REPORT_ERROR(
                "Invalid flush policy configuration, missing expected flush_count property for "
                "flush_policy=count: %s",
                logTargetCfg.c_str());
            return false;
        }
        uint32_t logCountLimit = 0;
        if (!parseIntProp("flush_count", logTargetCfg, itr2->second, logCountLimit, true)) {
            ELOG_REPORT_ERROR(
                "Invalid flush policy configuration, flush_count property value '%s' is an "
                "ill-formed integer: %s",
                itr2->second.c_str(), logTargetCfg.c_str());
            return false;
        }
        flushPolicy = new (std::nothrow) ELogCountFlushPolicy(logCountLimit);
    } else if (flushPolicyCfg.compare("size") == 0) {
        ELogPropertyMap::const_iterator itr2 = logTargetSpec.m_props.find("flush_size_bytes");
        if (itr2 == logTargetSpec.m_props.end()) {
            ELOG_REPORT_ERROR(
                "Invalid flush policy configuration, missing expected flush_size_bytes "
                "property for flush_policy=size: %s",
                logTargetCfg.c_str());
            return false;
        }
        uint32_t logSizeLimitBytes = 0;
        if (!parseIntProp("flush_size_bytes", logTargetCfg, itr2->second, logSizeLimitBytes,
                          true)) {
            ELOG_REPORT_ERROR(
                "Invalid flush policy configuration, flush_size_bytes property value '%s' is "
                "an ill-formed integer: %s",
                itr2->second.c_str(), logTargetCfg.c_str());
            return false;
        }
        flushPolicy = new (std::nothrow) ELogSizeFlushPolicy(logSizeLimitBytes);
    } else if (flushPolicyCfg.compare("time") == 0) {
        ELogPropertyMap::const_iterator itr2 =
            logTargetSpec.m_props.find("flush_timeout_millis");
        if (itr2 == logTargetSpec.m_props.end()) {
            ELOG_REPORT_ERROR(
                "Invalid flush policy configuration, missing expected flush_timeout_millis "
                "property for flush_policy=time: %s",
                logTargetCfg.c_str());
            return false;
        }
        uint32_t logTimeLimitMillis = 0;
        if (!parseIntProp("flush_timeout_millis", logTargetCfg, itr2->second,
                          logTimeLimitMillis, true)) {
            ELOG_REPORT_ERROR(
                "Invalid flush policy configuration, flush_timeout_millis property value '%s' "
                "is an ill-formed integer: %s",
                itr2->second.c_str(), logTargetCfg.c_str());
            return false;
        }
        flushPolicy = new (std::nothrow) ELogTimedFlushPolicy(logTimeLimitMillis, logTarget);
    } else if (flushPolicyCfg.compare("none") != 0) {
        ELOG_REPORT_ERROR("Unrecognized flush policy configuration %s: %s", flushPolicyCfg.c_str(),
                    logTargetCfg.c_str());
        return false;
    }
    logTarget->setFlushPolicy(flushPolicy);*/
    //}
    // return true;
}

bool ELogConfigLoader::applyTargetFilter(ELogTarget* logTarget, const std::string& logTargetCfg,
                                         const ELogTargetNestedSpec& logTargetSpec) {
    bool result = false;
    ELogFilter* filter = loadLogFilter(logTargetCfg, logTargetSpec, result);
    if (!result) {
        return false;
    }
    if (filter != nullptr) {
        logTarget->setLogFilter(filter);
    }
    return true;
    /*ELogPropertyMap::const_iterator itr = logTargetSpec.m_spec.m_props.find("filter");
    if (itr != logTargetSpec.m_spec.m_props.end()) {
        const std::string& filterCfg = itr->second;
        ELogFilter* filter = constructFilter(filterCfg.c_str());
        if (filter == nullptr) {
            ELOG_REPORT_ERROR("Failed to create filter by type %s: %s", filterCfg.c_str(),
                        logTargetCfg.c_str());
            return false;
        }
        if (!filter->load(logTargetCfg, logTargetSpec)) {
            ELOG_REPORT_ERROR("Failed to load filter by properties %s: %s", filterCfg.c_str(),
                        logTargetCfg.c_str());
            delete filter;
            return false;
        }

        // that's it
        logTarget->setLogFilter(filter);
    }
    return true;*/
}

ELogTarget* ELogConfigLoader::applyCompoundTarget(ELogTarget* logTarget,
                                                  const std::string& logTargetCfg,
                                                  const ELogTargetSpec& logTargetSpec,
                                                  bool& errorOccurred) {
    // there could be optional poperties: deferred,
    // queue_batch_size=<batch-size>,queue_timeout_millis=<timeout-millis>
    // quantum_buffer_size=<buffer-size>, quantum-congestion-policy=wait/discard
    errorOccurred = true;
    bool deferred = false;
    uint32_t queueBatchSize = 0;
    uint32_t queueTimeoutMillis = 0;
    uint32_t quantumBufferSize = 0;
    ELogQuantumTarget::CongestionPolicy congestionPolicy =
        ELogQuantumTarget::CongestionPolicy::CP_WAIT;
    for (const ELogProperty& prop : logTargetSpec.m_props) {
        // parse deferred property
        if (prop.first.compare("deferred") == 0) {
            if (queueBatchSize > 0 || queueTimeoutMillis > 0 || quantumBufferSize > 0) {
                ELOG_REPORT_ERROR(
                    "Deferred log target cannot be specified with queued or quantum target: %s",
                    logTargetCfg.c_str());
                return nullptr;
            }
            if (deferred) {
                ELOG_REPORT_ERROR("Deferred log target can be specified only once: %s",
                                  logTargetCfg.c_str());
                return nullptr;
            }
            deferred = true;
        }

        // parse queue batch size property
        else if (prop.first.compare("queue_batch_size") == 0) {
            if (deferred || quantumBufferSize > 0) {
                ELOG_REPORT_ERROR(
                    "Queued log target cannot be specified with deferred or quantum target: %s",
                    logTargetCfg.c_str());
                return nullptr;
            }
            if (queueBatchSize > 0) {
                ELOG_REPORT_ERROR("Queue batch size can be specified only once: %s",
                                  logTargetCfg.c_str());
                return nullptr;
            }
            if (!parseIntProp("queue_batch_size", logTargetCfg, prop.second, queueBatchSize)) {
                return nullptr;
            }
        }

        // parse queue timeout millis property
        else if (prop.first.compare("queue_timeout_millis") == 0) {
            if (deferred || quantumBufferSize > 0) {
                ELOG_REPORT_ERROR(
                    "Queued log target cannot be specified with deferred or quantum target: %s",
                    logTargetCfg.c_str());
                return nullptr;
            }
            if (queueTimeoutMillis > 0) {
                ELOG_REPORT_ERROR("Queue timeout millis can be specified only once: %s",
                                  logTargetCfg.c_str());
                return nullptr;
            }
            if (!parseIntProp("queue_timeout_millis", logTargetCfg, prop.second,
                              queueTimeoutMillis)) {
                return nullptr;
            }
        }

        // parse quantum buffer size
        else if (prop.first.compare("quantum_buffer_size") == 0) {
            if (deferred || queueBatchSize > 0 || queueTimeoutMillis > 0) {
                ELOG_REPORT_ERROR(
                    "Quantum log target cannot be specified with deferred or queued target: %s",
                    logTargetCfg.c_str());
                return nullptr;
            }
            if (quantumBufferSize > 0) {
                ELOG_REPORT_ERROR("Quantum buffer size can be specified only once: %s",
                                  logTargetCfg.c_str());
                return nullptr;
            }
            if (!parseIntProp("quantum_buffer_size", logTargetCfg, prop.second,
                              quantumBufferSize)) {
                return nullptr;
            }
        }

        // parse quantum buffer size
        else if (prop.first.compare("quantum-congestion-policy") == 0) {
            if (deferred || queueBatchSize > 0 || queueTimeoutMillis > 0) {
                ELOG_REPORT_ERROR(
                    "Quantum log target cannot be specified with deferred or queued target: %s",
                    logTargetCfg.c_str());
                return nullptr;
            }
            if (prop.second.compare("wait") == 0) {
                congestionPolicy = ELogQuantumTarget::CongestionPolicy::CP_WAIT;
            } else if (prop.second.compare("discard-log") == 0) {
                congestionPolicy = ELogQuantumTarget::CongestionPolicy::CP_DISCARD_LOG;
            } else if (prop.second.compare("discard-all") == 0) {
                congestionPolicy = ELogQuantumTarget::CongestionPolicy::CP_DISCARD_ALL;
            } else {
                ELOG_REPORT_ERROR("Invalid quantum log target congestion policy value '%s': %s",
                                  prop.second.c_str(), logTargetCfg.c_str());
                return nullptr;
            }
        }
    }

    if (queueBatchSize > 0 && queueTimeoutMillis == 0) {
        ELOG_REPORT_ERROR("Missing queue_timeout_millis parameter in log target specification: %s",
                          logTargetCfg.c_str());
        return nullptr;
    }

    if (queueBatchSize == 0 && queueTimeoutMillis > 0) {
        ELOG_REPORT_ERROR("Missing queue_batch_size parameter in log target specification: %s",
                          logTargetCfg.c_str());
        return nullptr;
    }

    ELogTarget* compoundLogTarget = nullptr;
    if (deferred) {
        compoundLogTarget = new (std::nothrow) ELogDeferredTarget(logTarget);
    } else if (queueBatchSize > 0) {
        compoundLogTarget =
            new (std::nothrow) ELogQueuedTarget(logTarget, queueBatchSize, queueTimeoutMillis);
    } else if (quantumBufferSize > 0) {
        compoundLogTarget =
            new (std::nothrow) ELogQuantumTarget(logTarget, quantumBufferSize, congestionPolicy);
    }

    errorOccurred = false;
    return compoundLogTarget;
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
        ELogFormatter* logFormatter = new (std::nothrow) ELogFormatter();
        if (logFormatter == nullptr) {
            ELOG_REPORT_ERROR(
                "Failed to allocate log formatter for log target, out of memory (context: %s)",
                logTargetCfg->getFullContext());
            return false;
        }
        if (!logFormatter->initialize(logFormat.c_str())) {
            ELOG_REPORT_ERROR("Invalid log format '%s' specified in log target (context: %s)",
                              logFormat.c_str(), logTargetCfg->getFullContext());
            delete logFormatter;
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