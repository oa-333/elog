#include "elog_filter.h"

#include <cstring>
#include <unordered_map>

#include "elog_config_loader.h"
#include "elog_error.h"
#include "elog_system.h"

namespace elog {

ELOG_IMPLEMENT_FILTER(ELogNegateFilter)
ELOG_IMPLEMENT_FILTER(ELogAndLogFilter)
ELOG_IMPLEMENT_FILTER(ELogOrLogFilter)
ELOG_IMPLEMENT_FILTER(ELogSourceFilter)
ELOG_IMPLEMENT_FILTER(ELogModuleFilter)
ELOG_IMPLEMENT_FILTER(ELogThreadNameFilter)
ELOG_IMPLEMENT_FILTER(ELogFileNameFilter)
ELOG_IMPLEMENT_FILTER(ELogFunctionNameFilter)
ELOG_IMPLEMENT_FILTER(ELogLevelFilter)

#define ELOG_MAX_FILTER_COUNT 100

struct ELogFilterNameConstructor {
    const char* m_name;
    ELogFilterConstructor* m_ctor;
};

static ELogFilterNameConstructor sFilterConstructors[ELOG_MAX_FILTER_COUNT] = {};
static uint32_t sFilterConstructorsCount = 0;

typedef std::unordered_map<std::string, ELogFilterConstructor*> ELogFilterConstructorMap;

static ELogFilterConstructorMap sFilterConstructorMap;

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

bool initFilters() { return applyFilterConstructorRegistration(); }

void termFilters() { sFilterConstructorMap.clear(); }

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

ELogNegateFilter::~ELogNegateFilter() {
    if (m_filter != nullptr) {
        delete m_filter;
        m_filter = nullptr;
    }
}

bool ELogNegateFilter::load(const std::string& logTargetCfg,
                            const ELogTargetNestedSpec& logTargetSpec) {
    // we expect to find a nested property 'filter_args' with one array item
    ELogTargetNestedSpec::SubSpecMap::const_iterator itr =
        logTargetSpec.m_subSpec.find("filter_args");
    if (itr == logTargetSpec.m_subSpec.end()) {
        ELOG_REPORT_ERROR("Missing 'filter_args' nested property required for NOT filter: %s",
                          logTargetCfg.c_str());
        return false;
    }

    const ELogTargetNestedSpec::SubSpecList& subSpecList = itr->second;
    if (subSpecList.empty()) {
        ELOG_REPORT_ERROR("Nested property 'filter_args' (required for NOT filter) is empty: %s",
                          logTargetCfg.c_str());
        return false;
    }
    if (subSpecList.size() > 1) {
        ELOG_REPORT_ERROR(
            "Nested property 'filter_args' (required for NOT filter) has more than one item: %s",
            logTargetCfg.c_str());
        return false;
    }
    const ELogTargetNestedSpec& subSpec = subSpecList[0];
    bool result = false;
    m_filter = ELogConfigLoader::loadLogFilter(logTargetCfg, logTargetSpec, result);
    if (!result) {
        ELOG_REPORT_ERROR("Failed to load sub-filter for NOT filter: %s (see errors above)",
                          logTargetCfg.c_str());
        return false;
    }
    if (m_filter == nullptr) {
        ELOG_REPORT_ERROR(
            "Failed to load sub-filter for NOT filter, filter specification not found: %s",
            logTargetCfg.c_str());
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

bool ELogCompoundLogFilter::load(const std::string& logTargetCfg,
                                 const ELogTargetNestedSpec& logTargetSpec) {
    // we expect to find a nested property 'filter_args' with one or more array item
    ELogTargetNestedSpec::SubSpecMap::const_iterator itr =
        logTargetSpec.m_subSpec.find("filter_args");
    if (itr == logTargetSpec.m_subSpec.end()) {
        ELOG_REPORT_ERROR("Missing 'filter_args' nested property required for compound filter: %s",
                          logTargetCfg.c_str());
        return false;
    }

    const ELogTargetNestedSpec::SubSpecList& subSpecList = itr->second;
    if (subSpecList.empty()) {
        ELOG_REPORT_ERROR(
            "Nested property 'filter_args' (required for compound filter) is empty: %s",
            logTargetCfg.c_str());
        return false;
    }
    for (uint32_t i = 0; i < subSpecList.size(); ++i) {
        const ELogTargetNestedSpec& subSpec = subSpecList[i];
        bool result = true;
        ELogFilter* filter = ELogConfigLoader::loadLogFilter(logTargetCfg, subSpec, result);
        if (!result) {
            ELOG_REPORT_ERROR(
                "Failed to load %uth sub-filter for compound filter: %s (see previous errors)", i,
                logTargetCfg.c_str());
            return false;
        }
        if (filter == nullptr) {
            ELOG_REPORT_ERROR(
                "Failed to load %uth sub-filter for compound filter, policy specification not "
                "found: %s",
                i, logTargetCfg.c_str());
            return false;
        }
        addFilter(filter);
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

bool ELogSourceFilter::load(const std::string& logTargetCfg,
                            const ELogTargetNestedSpec& logTargetSpec) {
    ELogPropertyMap::const_iterator itr = logTargetSpec.m_spec.m_props.find("log_source");
    if (itr == logTargetSpec.m_spec.m_props.end()) {
        ELOG_REPORT_ERROR("Log source filter missing 'log_source' specification: %s",
                          logTargetCfg.c_str());
        return false;
    }
    m_logSourceName = itr->second;
    return true;
}

bool ELogSourceFilter::filterLogRecord(const ELogRecord& logRecord) {
    ELogSource* logSource = ELogSystem::getLogSource(logRecord.m_sourceId);
    if (logSource == nullptr) {
        // this should not happen, we silently allow the message to pass...
        return true;
    }
    return m_logSourceName.compare(logSource->getQualifiedName());
}

bool ELogModuleFilter::load(const std::string& logTargetCfg,
                            const ELogTargetNestedSpec& logTargetSpec) {
    ELogPropertyMap::const_iterator itr = logTargetSpec.m_spec.m_props.find("log_module");
    if (itr == logTargetSpec.m_spec.m_props.end()) {
        ELOG_REPORT_ERROR("Log module filter missing 'log_module' specification: %s",
                          logTargetCfg.c_str());
        return false;
    }
    m_logModuleName = itr->second;
    return true;
}

bool ELogModuleFilter::filterLogRecord(const ELogRecord& logRecord) {
    ELogSource* logSource = ELogSystem::getLogSource(logRecord.m_sourceId);
    if (logSource == nullptr) {
        // this should not happen, we silently allow the message to pass...
        return true;
    }
    return m_logModuleName.compare(logSource->getModuleName());
}

bool ELogThreadNameFilter::load(const std::string& logTargetCfg,
                                const ELogTargetNestedSpec& logTargetSpec) {
    ELogPropertyMap::const_iterator itr = logTargetSpec.m_spec.m_props.find("thread_name");
    if (itr == logTargetSpec.m_spec.m_props.end()) {
        ELOG_REPORT_ERROR("Thread name filter missing 'thread_name' specification: %s",
                          logTargetCfg.c_str());
        return false;
    }
    m_threadName = itr->second;
    return true;
}

bool ELogThreadNameFilter::filterLogRecord(const ELogRecord& logRecord) {
    const char* threadName = getCurrentThreadNameField();
    if (threadName == nullptr) {
        return true;
    }
    return m_threadName.compare(threadName) == 0;
}

bool ELogFileNameFilter::load(const std::string& logTargetCfg,
                              const ELogTargetNestedSpec& logTargetSpec) {
    ELogPropertyMap::const_iterator itr = logTargetSpec.m_spec.m_props.find("file_name");
    if (itr == logTargetSpec.m_spec.m_props.end()) {
        ELOG_REPORT_ERROR("File name filter missing 'file_name' specification: %s",
                          logTargetCfg.c_str());
        return false;
    }
    m_fileName = itr->second;
    return true;
}

bool ELogFileNameFilter::filterLogRecord(const ELogRecord& logRecord) {
    // file name field may contain relative path, and searched path may be just bare name,
    // so we search for file name within the field
    return strstr(logRecord.m_file, m_fileName.c_str()) != nullptr;
}

bool ELogFunctionNameFilter::load(const std::string& logTargetCfg,
                                  const ELogTargetNestedSpec& logTargetSpec) {
    ELogPropertyMap::const_iterator itr = logTargetSpec.m_spec.m_props.find("function_name");
    if (itr == logTargetSpec.m_spec.m_props.end()) {
        ELOG_REPORT_ERROR("Function name filter missing 'function_name' specification: %s",
                          logTargetCfg.c_str());
        return false;
    }
    m_functionName = itr->second;
    return true;
}

bool ELogFunctionNameFilter::filterLogRecord(const ELogRecord& logRecord) {
    // function field can be very noisy so we just search for the name in it
    return strstr(logRecord.m_function, m_functionName.c_str()) != nullptr;
}

bool ELogLevelFilter::load(const std::string& logTargetCfg,
                           const ELogTargetNestedSpec& logTargetSpec) {
    ELogPropertyMap::const_iterator itr = logTargetSpec.m_spec.m_props.find("min_log_level");
    if (itr == logTargetSpec.m_spec.m_props.end()) {
        ELOG_REPORT_ERROR("Log level filter missing 'min_log_level' specification: %s",
                          logTargetCfg.c_str());
        return false;
    }
    if (!elogLevelFromStr(itr->second.c_str(), m_logLevel)) {
        ELOG_REPORT_ERROR("Invalid min_log_level value for log level filter: %s",
                          itr->second.c_str());
        return false;
    }
    return true;
}

bool ELogLevelFilter::filterLogRecord(const ELogRecord& logRecord) {
    return logRecord.m_logLevel <= m_logLevel;
}

}  // namespace elog
