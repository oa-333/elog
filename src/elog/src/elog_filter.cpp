#include "elog_filter.h"

#include <unordered_map>

#include "elog_error.h"

namespace elog {

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
    m_filter = ELogSystem::loadLogFilter(logTargetCfg, logTargetSpec, result);
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
        ELogFilter* filter = ELogSystem::loadLogFilter(logTargetCfg, subSpec, result);
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

}  // namespace elog
