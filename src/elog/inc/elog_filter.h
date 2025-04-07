#ifndef __ELOG_FILTER_H__
#define __ELOG_FILTER_H__

#include "elog_record.h"

namespace elog {

class ELogFilter {
public:
    virtual ~ELogFilter() {}

    virtual bool filterLogRecord(const ELogRecord& logRecord) = 0;

protected:
    ELogFilter() {}
};

class ELogNegateFilter : public ELogFilter {
public:
    ELogNegateFilter(ELogFilter* filter) : m_filter(filter) {}
    ~ELogNegateFilter() final {}

    bool filterLogRecord(const ELogRecord& logRecord) final {
        return m_filter->filterLogRecord(logRecord);
    }

private:
    ELogFilter* m_filter;
};

class ELogCompositeLogFilter : public ELogFilter {
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

class ELogAndLogFilter : public ELogCompositeLogFilter {
public:
    ELogAndLogFilter(ELogFilter* lhsFilter, ELogFilter* rhsFilter)
        : ELogCompositeLogFilter(lhsFilter, rhsFilter, ELogCompositeLogFilter::OpType::OT_AND) {}
    ~ELogAndLogFilter() final {}
};

class ELogOrLogFilter : public ELogCompositeLogFilter {
public:
    ELogOrLogFilter(ELogFilter* lhsFilter, ELogFilter* rhsFilter)
        : ELogCompositeLogFilter(lhsFilter, rhsFilter, ELogCompositeLogFilter::OpType::OT_OR) {}
    ~ELogOrLogFilter() final {}
};

}  // namespace elog

#endif  // __ELOG_FILTER_H__