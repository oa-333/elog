#include "elog_filter.h"

#include <cstring>
#include <regex>
#include <unordered_map>

#include "elog.h"
#include "elog_common.h"
#include "elog_config_loader.h"
#include "elog_error.h"
#include "elog_field_selector_internal.h"

namespace elog {

ELOG_IMPLEMENT_FILTER(ELogNotFilter)
ELOG_IMPLEMENT_FILTER(ELogAndLogFilter)
ELOG_IMPLEMENT_FILTER(ELogOrLogFilter)
ELOG_IMPLEMENT_FILTER(ELogRecordIdFilter);
ELOG_IMPLEMENT_FILTER(ELogRecordTimeFilter);
#if 0
ELOG_IMPLEMENT_FILTER(ELogHostNameFilter);
ELOG_IMPLEMENT_FILTER(ELogUserNameFilter);
ELOG_IMPLEMENT_FILTER(ELogProgramNameFilter);
ELOG_IMPLEMENT_FILTER(ELogProcessIdFilter);
ELOG_IMPLEMENT_FILTER(ELogThreadIdFilter);
#endif
ELOG_IMPLEMENT_FILTER(ELogThreadNameFilter);
ELOG_IMPLEMENT_FILTER(ELogSourceFilter)
ELOG_IMPLEMENT_FILTER(ELogModuleFilter)
ELOG_IMPLEMENT_FILTER(ELogFileNameFilter)
ELOG_IMPLEMENT_FILTER(ELogLineNumberFilter)
ELOG_IMPLEMENT_FILTER(ELogFunctionNameFilter)
ELOG_IMPLEMENT_FILTER(ELogLevelFilter)
ELOG_IMPLEMENT_FILTER(ELogMsgFilter)

#define ELOG_MAX_FILTER_COUNT 100

struct ELogFilterNameConstructor {
    const char* m_name;
    ELogFilterConstructor* m_ctor;
};

static ELogFilterNameConstructor sFilterConstructors[ELOG_MAX_FILTER_COUNT] = {};
static uint32_t sFilterConstructorsCount = 0;

typedef std::unordered_map<std::string, ELogFilterConstructor*> ELogFilterConstructorMap;

static ELogFilterConstructorMap sFilterConstructorMap;

typedef std::unordered_map<std::string, ELogCmpOp> ELogCmpOpMap;
static ELogCmpOpMap sCmpOpMap;

void registerFilterConstructor(const char* name, ELogFilterConstructor* constructor) {
    // due to c runtime issues we delay access to unordered map
    if (sFilterConstructorsCount >= ELOG_MAX_FILTER_COUNT) {
        ELOG_REPORT_ERROR("Cannot register filter constructor, no space: %s", name);
        exit(1);
    } else {
        sFilterConstructors[sFilterConstructorsCount++] = {name, constructor};
    }
}

static bool applyFilterConstructorRegistration() {
    for (uint32_t i = 0; i < sFilterConstructorsCount; ++i) {
        ELogFilterNameConstructor& nameCtorPair = sFilterConstructors[i];
        if (!sFilterConstructorMap
                 .insert(
                     ELogFilterConstructorMap::value_type(nameCtorPair.m_name, nameCtorPair.m_ctor))
                 .second) {
            ELOG_REPORT_ERROR("Duplicate filter identifier: %s", nameCtorPair.m_name);
            return false;
        }
    }
    return true;
}

static void initCmpOpMap() {
    sCmpOpMap["=="] = ELogCmpOp::CMP_OP_EQ;
    sCmpOpMap["!="] = ELogCmpOp::CMP_OP_NE;
    sCmpOpMap["<"] = ELogCmpOp::CMP_OP_LT;
    sCmpOpMap["<="] = ELogCmpOp::CMP_OP_LE;
    sCmpOpMap[">"] = ELogCmpOp::CMP_OP_GT;
    sCmpOpMap[">="] = ELogCmpOp::CMP_OP_GE;
    sCmpOpMap["CONTAINS"] = ELogCmpOp::CMP_OP_CONTAINS;
    sCmpOpMap["LIKE"] = ELogCmpOp::CMP_OP_LIKE;
    sCmpOpMap["contains"] = ELogCmpOp::CMP_OP_CONTAINS;
    sCmpOpMap["like"] = ELogCmpOp::CMP_OP_LIKE;
}

bool initFilters() {
    initCmpOpMap();
    return applyFilterConstructorRegistration();
}

void termFilters() {
    sFilterConstructorMap.clear();
    sCmpOpMap.clear();
}

ELogFilter* constructFilter(const char* name) {
    ELogFilterConstructorMap::iterator itr = sFilterConstructorMap.find(name);
    if (itr == sFilterConstructorMap.end()) {
        ELOG_REPORT_ERROR("Invalid filter %s: not found", name);
        return nullptr;
    }

    ELogFilterConstructor* constructor = itr->second;
    ELogFilter* filter = constructor->constructFilter();
    if (filter == nullptr) {
        ELOG_REPORT_ERROR("Failed to create filter, out of memory");
    }
    return filter;
}

template <typename T>
static bool compareInt(ELogCmpOp cmpOp, const T& lhs, const T& rhs) {
    switch (cmpOp) {
        case ELogCmpOp::CMP_OP_EQ:
            return lhs == rhs;
        case ELogCmpOp::CMP_OP_NE:
            return lhs != rhs;
        case ELogCmpOp::CMP_OP_LT:
            return lhs < rhs;
        case ELogCmpOp::CMP_OP_LE:
            return lhs <= rhs;
        case ELogCmpOp::CMP_OP_GT:
            return lhs > rhs;
        case ELogCmpOp::CMP_OP_GE:
            return lhs >= rhs;

        case ELogCmpOp::CMP_OP_CONTAINS:
        case ELogCmpOp::CMP_OP_LIKE:
        default:
            return false;
    }
}

inline bool compareString(ELogCmpOp cmpOp, const char* lhs, const char* rhs) {
    if (cmpOp == ELogCmpOp::CMP_OP_CONTAINS) {
        return strstr(lhs, rhs) != nullptr;
    } else if (cmpOp == ELogCmpOp::CMP_OP_LIKE) {
        // lhs is the string value from the log record
        // rhs is the pattern to be matched that was loaded from configuration
        std::regex pattern(rhs);
        return std::regex_match(lhs, pattern);
        // TODO: we can use regex replace for transforming log lines before shipping to external
        // sources (e.g. field/record obfuscation)
    }
    // otherwise do normal string comparison
    int cmpRes = strcmp(lhs, rhs);
    return compareInt<int>(cmpOp, cmpRes, 0);
}

inline bool compareLogLevel(ELogCmpOp cmpOp, ELogLevel lhs, ELogLevel rhs) {
    return compareInt<uint32_t>(cmpOp, lhs, rhs);
}

inline bool compareTime(ELogCmpOp cmpOp, ELogTime lhs, ELogTime rhs) {
    return compareInt<uint64_t>(cmpOp, elogTimeToUTCNanos(lhs), elogTimeToUTCNanos(rhs));
}

ELogNotFilter::~ELogNotFilter() {
    if (m_filter != nullptr) {
        delete m_filter;
        m_filter = nullptr;
    }
}

bool ELogNotFilter::load(const ELogConfigMapNode* filterCfg) {
    // we expect to find a nested property 'args' with one array item
    const ELogConfigValue* cfgValue = filterCfg->getValue("args");
    if (cfgValue == nullptr) {
        ELOG_REPORT_ERROR("Missing 'args' property required for NOT filter (context: %s)",
                          filterCfg->getFullContext());
        return false;
    }

    // expected array type
    if (cfgValue->getValueType() != ELogConfigValueType::ELOG_CONFIG_ARRAY_VALUE) {
        ELOG_REPORT_ERROR(
            "Invalid 'args' property type for NOT filter, expecting array, seeing instead %s "
            "(context: %s)",
            configValueTypeToString(cfgValue->getValueType()), cfgValue->getFullContext());
        return false;
    }
    const ELogConfigArrayNode* arrayNode = ((const ELogConfigArrayValue*)cfgValue)->getArrayNode();

    if (arrayNode->getValueCount() == 0) {
        ELOG_REPORT_ERROR("Nested property 'args' (required for NOT filter) is empty (context: %s)",
                          arrayNode->getFullContext());
        return false;
    }
    if (arrayNode->getValueCount() > 1) {
        ELOG_REPORT_ERROR(
            "Nested property 'args' (required for NOT filter) has more than one item (context: %s)",
            arrayNode->getFullContext());
        return false;
    }
    if (arrayNode->getValueAt(0)->getValueType() != ELogConfigValueType::ELOG_CONFIG_MAP_VALUE) {
        ELOG_REPORT_ERROR(
            "Invalid array property 'args' item type (required for NOT filter), expecting map, "
            "seeing instead %s (context: %s)",
            arrayNode->getFullContext());
        return false;
    }
    const ELogConfigMapNode* subFilterCfg =
        ((const ELogConfigMapValue*)arrayNode->getValueAt(0))->getMapNode();
    bool result = false;
    m_filter = ELogConfigLoader::loadLogFilter(subFilterCfg, result);
    if (!result) {
        ELOG_REPORT_ERROR("Failed to load sub-filter for NOT filter (context: %s)",
                          subFilterCfg->getFullContext());
        return false;
    }
    if (m_filter == nullptr) {
        ELOG_REPORT_ERROR(
            "Failed to load sub-filter for NOT filter, filter specification not found (context: "
            "%s)",
            subFilterCfg->getFullContext());
        return false;
    }
    return true;
}

ELogCompoundLogFilter::~ELogCompoundLogFilter() {
    for (ELogFilter* filter : m_filters) {
        delete filter;
    }
    m_filters.clear();
}

bool ELogCompoundLogFilter::load(const ELogConfigMapNode* filterCfg) {
    // we expect to find a nested property 'filter_args' with one or more array item
    const ELogConfigValue* cfgValue = filterCfg->getValue("filter_args");
    if (cfgValue == nullptr) {
        ELOG_REPORT_ERROR(
            "Missing 'filter_args' property required for compound log filter (context: %s)",
            filterCfg->getFullContext());
        return false;
    }

    // expected array type
    if (cfgValue->getValueType() != ELogConfigValueType::ELOG_CONFIG_ARRAY_VALUE) {
        ELOG_REPORT_ERROR(
            "Invalid 'filter_args' property type for compound log filter, expecting array, "
            "seeing instead %s (context: %s)",
            configValueTypeToString(cfgValue->getValueType()), cfgValue->getFullContext());
        return false;
    }
    const ELogConfigArrayNode* arrayNode = ((const ELogConfigArrayValue*)cfgValue)->getArrayNode();

    for (size_t i = 0; i < arrayNode->getValueCount(); ++i) {
        const ELogConfigValue* value = arrayNode->getValueAt(i);
        if (value->getValueType() != ELogConfigValueType::ELOG_CONFIG_MAP_VALUE) {
            ELOG_REPORT_ERROR(
                "Invalid sub-filter configuration value type, expecting map, seeing instead %s",
                configValueTypeToString(value->getValueType()), value->getFullContext());
            return false;
        }
        const ELogConfigMapNode* subFilterCfg = ((const ELogConfigMapValue*)value)->getMapNode();
        bool result = true;
        ELogFilter* subFilter = ELogConfigLoader::loadLogFilter(subFilterCfg, result);
        if (!result) {
            ELOG_REPORT_ERROR(
                "Failed to load %zuth sub-filter for compound log filter (context : %s)", i,
                subFilterCfg->getFullContext());
            return false;
        }
        if (subFilter == nullptr) {
            ELOG_REPORT_ERROR(
                "Failed to load %zuth sub-filter for compound log filter, filter specification not "
                "found (context: %s)",
                i, subFilterCfg->getFullContext());
            return false;
        }
        addFilter(subFilter);
    }
    return true;
}

bool ELogCompoundLogFilter::filterLogRecord(const ELogRecord& logRecord) {
    for (ELogFilter* filter : m_filters) {
        bool res = filter->filterLogRecord(logRecord);
        if (m_opType == OpType::OT_AND && !res) {
            // no need to compute second filter
            return false;
        } else if (m_opType == OpType::OT_OR && res) {
            // no need to compute second filter
            return true;
        }
    }

    // in case of AND filter, all passed so result is true
    // in case of OR filter, none have passed, so result is false
    return (m_opType == OpType::OT_AND) ? true : false;
}

static bool parseCmpOp(const char* cmpOpStr, ELogCmpOp& cmpOp) {
    if (strcasecmp(cmpOpStr, "EQ") == 0) {
        cmpOp = ELogCmpOp::CMP_OP_EQ;
        return true;
    }
    if (strcasecmp(cmpOpStr, "NE") == 0) {
        cmpOp = ELogCmpOp::CMP_OP_NE;
        return true;
    }
    if (strcasecmp(cmpOpStr, "LT") == 0) {
        cmpOp = ELogCmpOp::CMP_OP_LT;
        return true;
    }
    if (strcasecmp(cmpOpStr, "LE") == 0) {
        cmpOp = ELogCmpOp::CMP_OP_LE;
        return true;
    }
    if (strcasecmp(cmpOpStr, "GT") == 0) {
        cmpOp = ELogCmpOp::CMP_OP_GT;
        return true;
    }
    if (strcasecmp(cmpOpStr, "GE") == 0) {
        cmpOp = ELogCmpOp::CMP_OP_GE;
        return true;
    }
    if (strcasecmp(cmpOpStr, "LIKE") == 0) {
        cmpOp = ELogCmpOp::CMP_OP_LIKE;
        return true;
    }
    if (strcasecmp(cmpOpStr, "CONTAINS") == 0) {
        cmpOp = ELogCmpOp::CMP_OP_CONTAINS;
        return true;
    }
    return false;
}

static bool parseCmpOpCommon(const char* filterName, const ELogConfigMapNode* filterCfg,
                             ELogCmpOp& cmpOp, std::string* op = nullptr) {
    std::string opStr;
    bool found = false;
    if (!filterCfg->getStringValue("operator", found, opStr)) {
        ELOG_REPORT_ERROR("Failed to get operator string property for %s filter (context: %s)",
                          filterName, filterCfg->getFullContext());
        return false;
    }
    if (!found) {
        // if none specified we default to "equals"
        cmpOp = ELogCmpOp::CMP_OP_EQ;
        return true;
    }

    if (op != nullptr) {
        *op = opStr;
    }
    if (!parseCmpOp(opStr.c_str(), cmpOp)) {
        ELOG_REPORT_ERROR("Invalid operator '%s' specification for %s filter (context: %s)",
                          opStr.c_str(), filterName, filterCfg->getFullContext());
        return false;
    }

    return true;
}

static bool parseIntCmpOp(const char* filterName, const ELogConfigMapNode* filterCfg,
                          ELogCmpOp& cmpOp) {
    std::string op;
    if (!parseCmpOpCommon(filterName, filterCfg, cmpOp, &op)) {
        return false;
    }
    if (cmpOp == ELogCmpOp::CMP_OP_LIKE) {
        ELOG_REPORT_ERROR(
            "Invalid operator '%s' specification for %s filter, cannot specify regular "
            "expression for non-stirng operands (context: %s)",
            op.c_str(), filterName, filterCfg->getFullContext());
        return false;
    }

    return true;
}

static bool parseStringCmpOp(const char* filterName, const ELogConfigMapNode* filterCfg,
                             ELogCmpOp& cmpOp) {
    return parseCmpOpCommon(filterName, filterCfg, cmpOp);
}

bool elogCmpOpFromString(const char* cmpOpStr, ELogCmpOp& cmpOp) {
    ELogCmpOpMap::const_iterator itr = sCmpOpMap.find(cmpOpStr);
    if (itr == sCmpOpMap.end()) {
        ELOG_REPORT_ERROR("Invalid comparison operator '%s'", cmpOpStr);
        return false;
    }
    cmpOp = itr->second;
    return true;
}

bool ELogCmpFilter::loadStringFilter(const ELogConfigMapNode* filterCfg, const char* propertyName,
                                     const char* filterName, std::string& propertyValue) {
    bool found = false;
    if (filterCfg->getStringValue(propertyName, found, propertyValue)) {
        ELOG_REPORT_ERROR("Failed to get %s property for %s filter (context: %s)", propertyName,
                          filterName, filterCfg->getFullContext());
        return false;
    }
    if (!found) {
        ELOG_REPORT_ERROR("%s filter missing '%s' property (context: %s)", filterName, propertyName,
                          filterCfg->getFullContext());
        return false;
    }

    // get optional compare operator
    if (!parseStringCmpOp(filterName, filterCfg, m_cmpOp)) {
        return false;
    }
    return true;
}

bool ELogCmpFilter::loadIntFilter(const ELogConfigMapNode* filterCfg, const char* propertyName,
                                  const char* filterName, uint64_t& propertyValue) {
    bool found = false;
    int64_t value = 0;
    if (filterCfg->getIntValue(propertyName, found, value)) {
        ELOG_REPORT_ERROR("Failed to get %s property for %s filter (context: %s)", propertyName,
                          filterName, filterCfg->getFullContext());
        return false;
    }
    if (!found) {
        ELOG_REPORT_ERROR("%s filter missing '%s' property (context: %s)", filterName, propertyName,
                          filterCfg->getFullContext());
        return false;
    }
    propertyValue = (uint64_t)value;

    // get optional compare operator
    if (!parseIntCmpOp(filterName, filterCfg, m_cmpOp)) {
        return false;
    }
    return true;
}

bool ELogCmpFilter::loadStringFilter(const ELogExpression* expr, const char* filterName,
                                     std::string& value) {
    if (expr->m_type != ELogExpressionType::ET_OP_EXPR) {
        ELOG_REPORT_ERROR(
            "Invalid expression type, operator expression required for loading %s filter",
            filterName);
        return false;
    }
    const ELogOpExpression* opExpr = (const ELogOpExpression*)expr;
    if (!elogCmpOpFromString(opExpr->m_op.c_str(), m_cmpOp)) {
        ELOG_REPORT_ERROR("Invalid comparison operator '%s' for %s filter", opExpr->m_op.c_str(),
                          filterName);
        return false;
    }
    value = opExpr->m_rhs;
    return true;
}

bool ELogCmpFilter::loadIntFilter(const ELogExpression* expr, const char* filterName,
                                  uint64_t& value) {
    if (expr->m_type != ELogExpressionType::ET_OP_EXPR) {
        ELOG_REPORT_ERROR(
            "Invalid expression type, operator expression required for loading %s filter",
            filterName);
        return false;
    }
    const ELogOpExpression* opExpr = (const ELogOpExpression*)expr;
    if (!elogCmpOpFromString(opExpr->m_op.c_str(), m_cmpOp)) {
        ELOG_REPORT_ERROR("Invalid comparison operator '%s' for %s filter", opExpr->m_op.c_str(),
                          filterName);
        return false;
    }
    if (!parseIntProp("", "", opExpr->m_rhs, value, false)) {
        ELOG_REPORT_ERROR("Invalid expression operand '%s' for %s filter, required integer type",
                          opExpr->m_rhs.c_str(), filterName);
        return false;
    }
    return true;
}

bool ELogRecordIdFilter::load(const ELogConfigMapNode* filterCfg) {
    return loadIntFilter(filterCfg, "record_id", "record id", m_recordId);
}

bool ELogRecordIdFilter::loadExpr(const ELogExpression* expr) {
    return loadIntFilter(expr, "record_id", m_recordId);
}

bool ELogRecordIdFilter::filterLogRecord(const ELogRecord& logRecord) {
    return compareInt<decltype(logRecord.m_logRecordId)>(m_cmpOp, logRecord.m_logRecordId,
                                                         m_recordId);
}

bool ELogRecordTimeFilter::load(const ELogConfigMapNode* filterCfg) {
    // get mandatory property record_time
    std::string timeStr;
    if (!loadStringFilter(filterCfg, "record_time", "record id", timeStr)) {
        return false;
    }

    // parse time
    if (!elogTimeFromString(timeStr.c_str(), m_logTime)) {
        ELOG_REPORT_ERROR("Time specification %s for record time filter is invalid (context: %s)",
                          timeStr.c_str(), filterCfg->getFullContext());
        return false;
    }
    return true;
}

bool ELogRecordTimeFilter::loadExpr(const ELogExpression* expr) {
    // get mandatory property record_time
    std::string timeStr;
    if (!loadStringFilter(expr, "record_time", timeStr)) {
        return false;
    }

    // parse time
    if (!elogTimeFromString(timeStr.c_str(), m_logTime)) {
        ELOG_REPORT_ERROR("Time specification %s for record time filter is invalid",
                          timeStr.c_str());
        return false;
    }
    return true;
}

bool ELogRecordTimeFilter::filterLogRecord(const ELogRecord& logRecord) {
    return compareTime(m_cmpOp, logRecord.m_logTime, m_logTime);
}

#if 0
bool ELogHostNameFilter::load(const std::string& logTargetCfg,
                              const ELogTargetNestedSpec& logTargetSpec) {
    // get mandatory property host_name
    ELogPropertyMap::const_iterator itr = logTargetSpec.m_spec.m_props.find("host_name");
    if (itr == logTargetSpec.m_spec.m_props.end()) {
        ELOG_REPORT_ERROR("Host name filter missing 'host_name' specification: %s",
                          logTargetCfg.c_str());
        return false;
    }
    m_hostName = itr->second;

    // get optional compare operator
    if (!parseStringCmpOp("host name", logTargetCfg, logTargetSpec, m_cmpOp)) {
        return false;
    }
    return true;
}

bool ELogHostNameFilter::filterLogRecord(const ELogRecord& logRecord) {
    const char* hostName = getHostName();
    if (hostName == nullptr) {
        // this should not happen, we silently allow the message to pass...
        return true;
    }
    return compareString(m_cmpOp, logSource->getQualifiedName(), m_logSourceName.c_str());
}

bool ELogThreadIdFilter::load(const std::string& logTargetCfg,
                              const ELogTargetNestedSpec& logTargetSpec) {
    // get mandatory property thread_id
    ELogPropertyMap::const_iterator itr = logTargetSpec.m_spec.m_props.find("thread_id");
    if (itr == logTargetSpec.m_spec.m_props.end()) {
        ELOG_REPORT_ERROR("Thread name filter missing 'thread_name' specification: %s",
                          logTargetCfg.c_str());
        return false;
    }
    m_threadName = itr->second;

    // get optional compare operator
    if (!parseStringCmpOp("thread name", logTargetCfg, logTargetSpec, m_cmpOp)) {
        return false;
    }
    return true;
}

bool ELogThreadIdFilter::filterLogRecord(const ELogRecord& logRecord) {
    const char* threadName = getCurrentThreadNameField();
    if (threadName == nullptr || *threadName == 0) {
        return true;
    }
    return compareString(m_cmpOp, threadName, m_threadName.c_str());
}
#endif

bool ELogThreadNameFilter::load(const ELogConfigMapNode* filterCfg) {
    return loadStringFilter(filterCfg, "thread_name", "thread name", m_threadName);
}

bool ELogThreadNameFilter::loadExpr(const ELogExpression* expr) {
    return loadStringFilter(expr, "thread name", m_threadName);
}

bool ELogThreadNameFilter::filterLogRecord(const ELogRecord& logRecord) {
    const char* threadName = getCurrentThreadNameField();
    if (threadName == nullptr || *threadName == 0) {
        return true;
    }
    return compareString(m_cmpOp, threadName, m_threadName.c_str());
}

bool ELogSourceFilter::load(const ELogConfigMapNode* filterCfg) {
    return loadStringFilter(filterCfg, "log_source", "log source", m_logSourceName);
}

bool ELogSourceFilter::loadExpr(const ELogExpression* expr) {
    return loadStringFilter(expr, "log source", m_logSourceName);
}

bool ELogSourceFilter::filterLogRecord(const ELogRecord& logRecord) {
    size_t logSourceNameLength = 0;
    const char* logSourceName = getLogSourceName(logRecord, logSourceNameLength);
    return compareString(m_cmpOp, logSourceName, m_logSourceName.c_str());
}

bool ELogModuleFilter::load(const ELogConfigMapNode* filterCfg) {
    return loadStringFilter(filterCfg, "log_module", "log module", m_logModuleName);
}

bool ELogModuleFilter::loadExpr(const ELogExpression* expr) {
    return loadStringFilter(expr, "log module", m_logModuleName);
}

bool ELogModuleFilter::filterLogRecord(const ELogRecord& logRecord) {
    size_t moduleNameLength = 0;
    const char* moduleName = getLogModuleName(logRecord, moduleNameLength);
    return compareString(m_cmpOp, moduleName, m_logModuleName.c_str());
}

bool ELogFileNameFilter::load(const ELogConfigMapNode* filterCfg) {
    return loadStringFilter(filterCfg, "file_name", "file name", m_fileName);
}

bool ELogFileNameFilter::loadExpr(const ELogExpression* expr) {
    return loadStringFilter(expr, "file name", m_fileName);
}

bool ELogFileNameFilter::filterLogRecord(const ELogRecord& logRecord) {
    return compareString(m_cmpOp, logRecord.m_file, m_fileName.c_str());
}

bool ELogLineNumberFilter::load(const ELogConfigMapNode* filterCfg) {
    uint64_t lineNumber = 0;
    if (!loadIntFilter(filterCfg, "line_number", "line number", lineNumber)) {
        return false;
    }
    m_lineNumber = (int)lineNumber;
    return true;
}

bool ELogLineNumberFilter::loadExpr(const ELogExpression* expr) {
    uint64_t lineNumber = 0;
    if (!loadIntFilter(expr, "line number", lineNumber)) {
        return false;
    }
    m_lineNumber = (int)lineNumber;
    return true;
}

bool ELogLineNumberFilter::filterLogRecord(const ELogRecord& logRecord) {
    return compareInt<int>(m_cmpOp, (int)logRecord.m_line, m_lineNumber);
}

bool ELogFunctionNameFilter::load(const ELogConfigMapNode* filterCfg) {
    return loadStringFilter(filterCfg, "function_name", "function name", m_functionName);
}

bool ELogFunctionNameFilter::loadExpr(const ELogExpression* expr) {
    return loadStringFilter(expr, "function name", m_functionName);
}

bool ELogFunctionNameFilter::filterLogRecord(const ELogRecord& logRecord) {
    return compareString(m_cmpOp, logRecord.m_function, m_functionName.c_str());
}

bool ELogLevelFilter::load(const ELogConfigMapNode* filterCfg) {
    std::string logLevelStr;
    if (!loadStringFilter(filterCfg, "log_level", "log level", logLevelStr)) {
        return false;
    }

    // parse time
    if (!elogLevelFromStr(logLevelStr.c_str(), m_logLevel)) {
        ELOG_REPORT_ERROR(
            "Invalid log_level value '%s' specified for log level filter (context: %s)",
            logLevelStr.c_str(), filterCfg->getFullContext());
        return false;
    }
    return true;
}

bool ELogLevelFilter::loadExpr(const ELogExpression* expr) {
    std::string logLevelStr;
    if (!loadStringFilter(expr, "log level", logLevelStr)) {
        return false;
    }

    // parse time
    if (!elogLevelFromStr(logLevelStr.c_str(), m_logLevel)) {
        ELOG_REPORT_ERROR("Invalid log_level value '%s' specified for log level filter",
                          logLevelStr.c_str());
        return false;
    }
    return true;
}

bool ELogLevelFilter::filterLogRecord(const ELogRecord& logRecord) {
    return compareLogLevel(m_cmpOp, logRecord.m_logLevel, m_logLevel);
}

bool ELogMsgFilter::load(const ELogConfigMapNode* filterCfg) {
    return loadStringFilter(filterCfg, "log_msg", "log message", m_logMsg);
}

bool ELogMsgFilter::loadExpr(const ELogExpression* expr) {
    return loadStringFilter(expr, "log message", m_logMsg);
}

bool ELogMsgFilter::filterLogRecord(const ELogRecord& logRecord) {
    return compareString(m_cmpOp, logRecord.m_logMsg, m_logMsg.c_str());
}

}  // namespace elog
