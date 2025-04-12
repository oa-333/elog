#ifndef __ELOG_FILTER_H__
#define __ELOG_FILTER_H__

#include "elog_def.h"
#include "elog_record.h"

namespace elog {

/** @brief Parent interface for all log filters. */
class ELOG_API ELogFilter {
public:
    virtual ~ELogFilter() {}

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

/** @brief A log filter that negates the result of another log filter. */
class ELOG_API ELogNegateFilter : public ELogFilter {
public:
    ELogNegateFilter(ELogFilter* filter) : m_filter(filter) {}
    ~ELogNegateFilter() final {}

    bool filterLogRecord(const ELogRecord& logRecord) final {
        return m_filter->filterLogRecord(logRecord);
    }

private:
    ELogFilter* m_filter;
};

/**
 * @brief A composite log filter that combines the result of two other log filters by either
 * applying AND operator on the result or applying OR operator on the result of the underlying two
 * filters.
 */
class ELOG_API ELogCompositeLogFilter : public ELogFilter {
public:
    enum class OpType { OT_AND, OT_OR };

    ELogCompositeLogFilter(ELogFilter* lhsFilter, ELogFilter* rhsFilter, OpType opType)
        : m_lhsFilter(lhsFilter), m_rhsFilter(rhsFilter), m_opType(opType) {}
    ~ELogCompositeLogFilter() override {}

    bool filterLogRecord(const ELogRecord& logRecord) final {
        bool lhsRes = m_lhsFilter->filterLogRecord(logRecord);
        if (m_opType == OpType::OT_AND && !lhsRes) {
            // no need to compute second filter
            return false;
        } else if (m_opType == OpType::OT_OR && lhsRes) {
            // no need to compute second filter
            return true;
        }

        // result determined by RHS filter
        return m_rhsFilter->filterLogRecord(logRecord);
    }

private:
    ELogFilter* m_lhsFilter;
    ELogFilter* m_rhsFilter;
    OpType m_opType;
};

/**
 * @brief An AND log filter that checks both underlying filters allow the record to be processed.
 */
class ELOG_API ELogAndLogFilter : public ELogCompositeLogFilter {
public:
    ELogAndLogFilter(ELogFilter* lhsFilter, ELogFilter* rhsFilter)
        : ELogCompositeLogFilter(lhsFilter, rhsFilter, ELogCompositeLogFilter::OpType::OT_AND) {}
    ~ELogAndLogFilter() final {}
};

/**
 * @brief An OR log filter that checks if either one of the underlying filters allows the record to
 * be processed.
 */
class ELOG_API ELogOrLogFilter : public ELogCompositeLogFilter {
public:
    ELogOrLogFilter(ELogFilter* lhsFilter, ELogFilter* rhsFilter)
        : ELogCompositeLogFilter(lhsFilter, rhsFilter, ELogCompositeLogFilter::OpType::OT_OR) {}
    ~ELogOrLogFilter() final {}
};

}  // namespace elog

#endif  // __ELOG_FILTER_H__