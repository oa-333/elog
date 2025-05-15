#ifndef __ELOG_FILTER_H__
#define __ELOG_FILTER_H__

#include "elog_common.h"
#include "elog_def.h"
#include "elog_record.h"

namespace elog {

/** @brief Initialize all filters (for internal use only). */
extern ELOG_API bool initFilters();

/** @brief Destroys all filters (for internal use only). */
extern ELOG_API void termFilters();

/** @brief Parent interface for all log filters. */
class ELOG_API ELogFilter {
public:
    virtual ~ELogFilter() {}

    /** @brief Loads filter from configuration. */
    virtual bool load(const std::string& logTargetCfg, const ELogTargetNestedSpec& logTargetSpec) {
        return true;
    }

    /**
     * @brief Filters a log record.
     * @param logRecord The log record to filter.
     * @return true If the log record is to be logged.
     * @return false If the log record is to be discarded.
     */
    virtual bool filterLogRecord(const ELogRecord& logRecord) = 0;

protected:
    ELogFilter() {}
};

// forward declaration
class ELOG_API ELogFilterConstructor;

/**
 * @brief Filter constructor registration helper.
 * @param name The filter identifier.
 * @param allocator The filter constructor.
 */
extern ELOG_API void registerFilterConstructor(const char* name,
                                               ELogFilterConstructor* constructor);

/**
 * @brief Utility helper for constructing a filter from type name identifier.
 * @param name The filter identifier.
 * @return ELogFilter* The resulting filter, or null if failed.
 */
extern ELOG_API ELogFilter* constructFilter(const char* name);

/** @brief Utility helper class for filter construction. */
class ELOG_API ELogFilterConstructor {
public:
    /**
     * @brief Constructs a filter.
     * @return ELogFilter* The resulting filter, or null if failed.
     */
    virtual ELogFilter* constructFilter() = 0;

protected:
    /** @brief Constructor. */
    ELogFilterConstructor(const char* name) { registerFilterConstructor(name, this); }
};

/** @def Utility macro for declaring filter factory method registration. */
#define ELOG_DECLARE_FILTER(FilterType, Name)                                                 \
    class FilterType##Constructor : public elog::ELogFilterConstructor {                      \
    public:                                                                                   \
        FilterType##Constructor() : elog::ELogFilterConstructor(#Name) {}                     \
        elog::ELogFilter* constructFilter() final { return new (std::nothrow) FilterType(); } \
    };                                                                                        \
    static FilterType##Constructor sConstructor;

/** @def Utility macro for implementing filter factory method registration. */
#define ELOG_IMPLEMENT_FILTER(FilterType) \
    FilterType::FilterType##Constructor FilterType::sConstructor;

/** @brief A log filter that negates the result of another log filter. */
class ELOG_API ELogNegateFilter : public ELogFilter {
public:
    ELogNegateFilter() : m_filter(nullptr) {}
    ELogNegateFilter(ELogFilter* filter) : m_filter(filter) {}
    ~ELogNegateFilter() final;

    /** @brief Loads filter from property map. */
    bool load(const std::string& logTargetCfg, const ELogTargetNestedSpec& logTargetSpec) final;

    /**
     * @brief Filters a log record.
     * @param logRecord The log record to filter.
     * @return true If the log record is to be logged.
     * @return false If the log record is to be discarded.
     */
    bool filterLogRecord(const ELogRecord& logRecord) final {
        return !m_filter->filterLogRecord(logRecord);
    }

private:
    ELogFilter* m_filter;

    ELOG_DECLARE_FILTER(ELogNegateFilter, not);
};

/**
 * @brief A compound log filter that combines the result of two or more other log filters by either
 * applying AND operator on the result or applying OR operator on the result of the underlying
 * filters.
 */
class ELOG_API ELogCompoundLogFilter : public ELogFilter {
public:
    enum class OpType { OT_AND, OT_OR };

    ELogCompoundLogFilter(OpType opType) : m_opType(opType) {}
    ~ELogCompoundLogFilter() override;

    /** @brief Adds a sub-filter to the filter set. */
    inline void addFilter(ELogFilter* filter) { m_filters.push_back(filter); }

    /** @brief Loads filter from property map. */
    bool load(const std::string& logTargetCfg, const ELogTargetNestedSpec& logTargetSpec) final;

    /**
     * @brief Filters a log record.
     * @param logRecord The log record to filter.
     * @return true If the log record is to be logged.
     * @return false If the log record is to be discarded.
     */
    bool filterLogRecord(const ELogRecord& logRecord) final;

private:
    std::vector<ELogFilter*> m_filters;
    OpType m_opType;
};

/**
 * @brief An AND log filter that checks both underlying filters allow the record to be processed.
 */
class ELOG_API ELogAndLogFilter : public ELogCompoundLogFilter {
public:
    ELogAndLogFilter() : ELogCompoundLogFilter(ELogCompoundLogFilter::OpType::OT_AND) {}
    ~ELogAndLogFilter() final {}

private:
    ELOG_DECLARE_FILTER(ELogAndLogFilter, and);
};

/**
 * @brief An OR log filter that checks if either one of the underlying filters allows the record to
 * be processed.
 */
class ELOG_API ELogOrLogFilter : public ELogCompoundLogFilter {
public:
    ELogOrLogFilter() : ELogCompoundLogFilter(ELogCompoundLogFilter::OpType::OT_OR) {}
    ~ELogOrLogFilter() final {}

private:
    ELOG_DECLARE_FILTER(ELogOrLogFilter, or);
};

}  // namespace elog

#endif  // __ELOG_FILTER_H__