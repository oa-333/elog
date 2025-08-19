#ifndef __ELOG_FILTER_H__
#define __ELOG_FILTER_H__

#include "elog_config.h"
#include "elog_expression.h"
#include "elog_managed_object.h"
#include "elog_record.h"
#include "elog_target_spec.h"

namespace elog {

/** @brief Parent interface for all log filters. */
class ELOG_API ELogFilter : public ELogManagedObject {
public:
    virtual ~ELogFilter() {}

    /** @brief Loads filter from configuration. */
    virtual bool load(const ELogConfigMapNode* filterCfg) { return true; }

    /** @brief Loads filter from a free-style predicate-like parsed expression. */
    virtual bool loadExpr(const ELogExpression* expr) { return true; };

    /**
     * @brief Filters a log record.
     * @param logRecord The log record to filter.
     * @return true If the log record is to be logged.
     * @return false If the log record is to be discarded.
     */
    virtual bool filterLogRecord(const ELogRecord& logRecord) = 0;

protected:
    ELogFilter() {}
    ELogFilter(const ELogFilter&) = delete;
    ELogFilter(ELogFilter&&) = delete;
    ELogFilter& operator=(const ELogFilter&) = delete;
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
    virtual ~ELogFilterConstructor() {}

    /**
     * @brief Constructs a filter.
     * @return ELogFilter* The resulting filter, or null if failed.
     */
    virtual ELogFilter* constructFilter() = 0;

protected:
    /** @brief Constructor. */
    ELogFilterConstructor(const char* name) { registerFilterConstructor(name, this); }
    ELogFilterConstructor(ELogFilterConstructor&) = delete;
    ELogFilterConstructor(ELogFilterConstructor&&) = delete;
    ELogFilterConstructor& operator=(const ELogFilterConstructor&) = delete;
};

/** @def Utility macro for declaring filter factory method registration. */
#define ELOG_DECLARE_FILTER(FilterType, Name)                                                 \
    class ELOG_API FilterType##Constructor : public elog::ELogFilterConstructor {             \
    public:                                                                                   \
        FilterType##Constructor() : elog::ELogFilterConstructor(#Name) {}                     \
        elog::ELogFilter* constructFilter() final { return new (std::nothrow) FilterType(); } \
        ~FilterType##Constructor() final {}                                                   \
        FilterType##Constructor(FilterType##Constructor&) = delete;                           \
        FilterType##Constructor(FilterType##Constructor&&) = delete;                          \
        FilterType##Constructor& operator=(const FilterType##Constructor&) = delete;          \
    };                                                                                        \
    static FilterType##Constructor sConstructor;

/** @def Utility macro for implementing filter factory method registration. */
#define ELOG_IMPLEMENT_FILTER(FilterType) \
    FilterType::FilterType##Constructor FilterType::sConstructor;

/** @brief A log filter that negates the result of another log filter. */
class ELOG_API ELogNotFilter : public ELogFilter {
public:
    ELogNotFilter() : m_filter(nullptr) {}
    ELogNotFilter(ELogFilter* filter) : m_filter(filter) {}
    ELogNotFilter(const ELogNotFilter&) = delete;
    ELogNotFilter(ELogNotFilter&&) = delete;
    ELogNotFilter& operator=(const ELogNotFilter&) = delete;
    ~ELogNotFilter() final;

    /** @brief Loads filter from configuration. */
    bool load(const ELogConfigMapNode* filterCfg) final;

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

    ELOG_DECLARE_FILTER(ELogNotFilter, NOT);
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
    ELogCompoundLogFilter(const ELogCompoundLogFilter&) = delete;
    ELogCompoundLogFilter(ELogCompoundLogFilter&&) = delete;
    ELogCompoundLogFilter& operator=(const ELogCompoundLogFilter&) = delete;
    ~ELogCompoundLogFilter() override;

    /** @brief Adds a sub-filter to the filter set. */
    inline void addFilter(ELogFilter* filter) { m_filters.push_back(filter); }

    /** @brief Loads filter from configuration. */
    bool load(const ELogConfigMapNode* filterCfg) final;

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
    ELogAndLogFilter(const ELogAndLogFilter&) = delete;
    ELogAndLogFilter(ELogAndLogFilter&&) = delete;
    ELogAndLogFilter& operator=(const ELogAndLogFilter&) = delete;
    ~ELogAndLogFilter() final {}

private:
    ELOG_DECLARE_FILTER(ELogAndLogFilter, AND);
};

/**
 * @brief An OR log filter that checks if either one of the underlying filters allows the record to
 * be processed.
 */
class ELOG_API ELogOrLogFilter : public ELogCompoundLogFilter {
public:
    ELogOrLogFilter() : ELogCompoundLogFilter(ELogCompoundLogFilter::OpType::OT_OR) {}
    ELogOrLogFilter(const ELogOrLogFilter&) = delete;
    ELogOrLogFilter(ELogOrLogFilter&&) = delete;
    ELogOrLogFilter& operator=(const ELogOrLogFilter&) = delete;
    ~ELogOrLogFilter() final {}

private:
    ELOG_DECLARE_FILTER(ELogOrLogFilter, OR);
};

/** @brief Comparison operation constants. */
enum class ELogCmpOp : uint32_t {
    /** @brief Designates equals comparison. */
    CMP_OP_EQ,

    /** @brief Designates not-equals comparison. */
    CMP_OP_NE,

    /** @brief Designates less-than comparison. */
    CMP_OP_LT,

    /** @brief Designates less-than or equals-to comparison. */
    CMP_OP_LE,

    /** @brief Designates greater-than comparison. */
    CMP_OP_GT,

    /** @brief Designates greater-than or equals-to comparison. */
    CMP_OP_GE,

    /** @brief Designates regular expression matching (strings only). */
    CMP_OP_LIKE,

    /** @brief Designates substring matching (strings only). */
    CMP_OP_CONTAINS
};

/** @brief Convert a string to a comparison operator. */
extern ELOG_API bool elogCmpOpFromString(const char* cmpOpStr, ELogCmpOp& cmpOp);

class ELOG_API ELogCmpFilter : public ELogFilter {
public:
protected:
    ELogCmpFilter(ELogCmpOp cmpOp) : m_cmpOp(cmpOp) {}
    ELogCmpFilter(const ELogCmpFilter&) = delete;
    ELogCmpFilter(ELogCmpFilter&&) = delete;
    ELogCmpFilter& operator=(const ELogCmpFilter&) = delete;
    ~ELogCmpFilter() override {}

    ELogCmpOp m_cmpOp;

    bool loadStringFilter(const ELogConfigMapNode* filterCfg, const char* propertyName,
                          const char* filterName, std::string& propertyValue);
    bool loadIntFilter(const ELogConfigMapNode* filterCfg, const char* propertyName,
                       const char* filterName, uint64_t& propertyValue);

    bool loadStringFilter(const ELogExpression* expr, const char* filterName, std::string& value);
    bool loadIntFilter(const ELogExpression* expr, const char* filterName, uint64_t& value);
};

class ELOG_API ELogRecordIdFilter : public ELogCmpFilter {
public:
    ELogRecordIdFilter(uint64_t recordId = 0, ELogCmpOp cmpOp = ELogCmpOp::CMP_OP_GE)
        : ELogCmpFilter(cmpOp), m_recordId(recordId) {}
    ELogRecordIdFilter(const ELogRecordIdFilter&) = delete;
    ELogRecordIdFilter(ELogRecordIdFilter&&) = delete;
    ELogRecordIdFilter& operator=(const ELogRecordIdFilter&) = delete;
    ~ELogRecordIdFilter() override {}

    /** @brief Loads filter from configuration. */
    bool load(const ELogConfigMapNode* filterCfg) final;

    /** @brief Loads filter from a free-style predicate-like parsed expression. */
    bool loadExpr(const ELogExpression* expr) final;

    /**
     * @brief Filters a log record.
     * @param logRecord The log record to filter.
     * @return true If the log record is to be logged.
     * @return false If the log record is to be discarded.
     */
    bool filterLogRecord(const ELogRecord& logRecord) final;

private:
    uint64_t m_recordId;

    ELOG_DECLARE_FILTER(ELogRecordIdFilter, record_id);
};

class ELOG_API ELogRecordTimeFilter : public ELogCmpFilter {
public:
    ELogRecordTimeFilter() : ELogCmpFilter(ELogCmpOp::CMP_OP_GE) {}
    ELogRecordTimeFilter(ELogTime logTime, ELogCmpOp cmpOp = ELogCmpOp::CMP_OP_GE)
        : ELogCmpFilter(cmpOp), m_logTime(logTime) {}
    ELogRecordTimeFilter(const ELogRecordTimeFilter&) = delete;
    ELogRecordTimeFilter(ELogRecordTimeFilter&&) = delete;
    ELogRecordTimeFilter& operator=(const ELogRecordTimeFilter&) = delete;
    ~ELogRecordTimeFilter() override {}

    /** @brief Loads filter from configuration. */
    bool load(const ELogConfigMapNode* filterCfg) final;

    /** @brief Loads filter from a free-style predicate-like parsed expression. */
    bool loadExpr(const ELogExpression* expr) final;

    /**
     * @brief Filters a log record.
     * @param logRecord The log record to filter.
     * @return true If the log record is to be logged.
     * @return false If the log record is to be discarded.
     */
    bool filterLogRecord(const ELogRecord& logRecord) final;

private:
    ELogTime m_logTime;

    ELOG_DECLARE_FILTER(ELogRecordTimeFilter, record_time);
};

// TODO: the filters below will be useful when we have some kind of dispatch hub/service, that reads
// records from many loggers and then dispatches them to some server. right now they are disabled
// the process/thread id filters make no sense, unless we apply some live filters during runtime
// (i.e. as a result of some controlling dashboard for the purpose of live RCA)
#if 0
class ELOG_API ELogHostNameFilter : public ELogCmpFilter {
public:
    ELogHostNameFilter(const char* hostName = "", ELogCmpOp cmpOp = ELogCmpOp::CMP_OP_EQ)
        : ELogCmpFilter(cmpOp), m_hostName(hostName) {}
    ELogHostNameFilter(const ELogHostNameFilter&) = delete;
    ELogHostNameFilter(ELogHostNameFilter&&) = delete;
    ELogHostNameFilter& operator=(const ELogHostNameFilter&) = delete;
    ~ELogHostNameFilter() override {}

    /** @brief Loads filter from configuration. */
    bool load(const ELogConfigMapNode* filterCfg) final;

    /** @brief Loads filter from a free-style predicate-like parsed expression. */
    bool load(const ELogExpression* expr) final;

    /**
     * @brief Filters a log record.
     * @param logRecord The log record to filter.
     * @return true If the log record is to be logged.
     * @return false If the log record is to be discarded.
     */
    bool filterLogRecord(const ELogRecord& logRecord) final;

private:
    std::string m_hostName;

    ELOG_DECLARE_FILTER(ELogHostNameFilter, host_name);
};

class ELOG_API ELogUserNameFilter : public ELogCmpFilter {
public:
    ELogUserNameFilter(const char* userName = "", ELogCmpOp cmpOp = ELogCmpOp::CMP_OP_EQ)
        : ELogCmpFilter(cmpOp), m_userName(userName) {}
    ELogUserNameFilter(const ELogUserNameFilter&) = delete;
    ELogUserNameFilter(ELogUserNameFilter&&) = delete;
    ELogUserNameFilter& operator=(const ELogUserNameFilter&) = delete;
    ~ELogUserNameFilter() override {}

    /** @brief Loads filter from configuration. */
    bool load(const ELogConfigMapNode* filterCfg) final;

    /** @brief Loads filter from a free-style predicate-like parsed expression. */
    bool load(const ELogExpression* expr) final;

    /**
     * @brief Filters a log record.
     * @param logRecord The log record to filter.
     * @return true If the log record is to be logged.
     * @return false If the log record is to be discarded.
     */
    bool filterLogRecord(const ELogRecord& logRecord) final;

private:
    std::string m_userName;

    ELOG_DECLARE_FILTER(ELogUserNameFilter, user_name);
};

class ELOG_API ELogProgramNameFilter : public ELogCmpFilter {
public:
    ELogProgramNameFilter(const char* programName = "", ELogCmpOp cmpOp = ELogCmpOp::CMP_OP_EQ)
        : ELogCmpFilter(cmpOp), m_programName(programName) {}
    ELogProgramNameFilter(const ELogProgramNameFilter&) = delete;
    ELogProgramNameFilter(ELogProgramNameFilter&&) = delete;
    ELogProgramNameFilter& operator=(const ELogProgramNameFilter&) = delete;
    ~ELogProgramNameFilter() override {}

    /** @brief Loads filter from configuration. */
    bool load(const ELogConfigMapNode* filterCfg) final;

    /** @brief Loads filter from a free-style predicate-like parsed expression. */
    bool load(const ELogExpression* expr) final;

    /**
     * @brief Filters a log record.
     * @param logRecord The log record to filter.
     * @return true If the log record is to be logged.
     * @return false If the log record is to be discarded.
     */
    bool filterLogRecord(const ELogRecord& logRecord) final;

private:
    std::string m_programName;

    ELOG_DECLARE_FILTER(ELogProgramNameFilter, program_name);
};

class ELOG_API ELogProcessIdFilter : public ELogCmpFilter {
public:
    ELogProcessIdFilter(const char* processIdName = "", ELogCmpOp cmpOp = ELogCmpOp::CMP_OP_EQ)
        : ELogCmpFilter(cmpOp), m_processIdName(processIdName) {}
    ELogProcessIdFilter(const ELogProcessIdFilter&) = delete;
    ELogProcessIdFilter(ELogProcessIdFilter&&) = delete;
    ELogProcessIdFilter& operator=(const ELogProcessIdFilter&) = delete;
    ~ELogProcessIdFilter() override {}

    /** @brief Loads filter from configuration. */
    bool load(const ELogConfigMapNode* filterCfg) final;

    /** @brief Loads filter from a free-style predicate-like parsed expression. */
    bool load(const ELogExpression* expr) final;

    /**
     * @brief Filters a log record.
     * @param logRecord The log record to filter.
     * @return true If the log record is to be logged.
     * @return false If the log record is to be discarded.
     */
    bool filterLogRecord(const ELogRecord& logRecord) final;

private:
    std::string m_processIdName;

    ELOG_DECLARE_FILTER(ELogProcessIdFilter, process_id_name);
};

class ELOG_API ELogThreadIdFilter : public ELogCmpFilter {
public:
    ELogThreadIdFilter(const char* threadIdName = "", ELogCmpOp cmpOp = ELogCmpOp::CMP_OP_EQ)
        : ELogCmpFilter(cmpOp), m_threadIdName(threadIdName) {}
    ELogThreadIdFilter(const ELogThreadIdFilter&) = delete;
    ELogThreadIdFilter(ELogThreadIdFilter&&) = delete;
    ELogThreadIdFilter& operator=(const ELogThreadIdFilter&) = delete;
    ~ELogThreadIdFilter() override {}

    /** @brief Loads filter from configuration. */
    bool load(const ELogConfigMapNode* filterCfg) final;

    /** @brief Loads filter from a free-style predicate-like parsed expression. */
    bool load(const ELogExpression* expr) final;

    /**
     * @brief Filters a log record.
     * @param logRecord The log record to filter.
     * @return true If the log record is to be logged.
     * @return false If the log record is to be discarded.
     */
    bool filterLogRecord(const ELogRecord& logRecord) final;

private:
    std::string m_threadIdName;

    ELOG_DECLARE_FILTER(ELogThreadIdFilter, thread_id_name);
};
#endif

class ELOG_API ELogThreadNameFilter : public ELogCmpFilter {
public:
    ELogThreadNameFilter(const char* threadName = "", ELogCmpOp cmpOp = ELogCmpOp::CMP_OP_EQ)
        : ELogCmpFilter(cmpOp), m_threadName(threadName) {}
    ELogThreadNameFilter(const ELogThreadNameFilter&) = delete;
    ELogThreadNameFilter(ELogThreadNameFilter&&) = delete;
    ELogThreadNameFilter& operator=(const ELogThreadNameFilter&) = delete;
    ~ELogThreadNameFilter() final {}

    /** @brief Loads filter from configuration. */
    bool load(const ELogConfigMapNode* filterCfg) final;

    /** @brief Loads filter from a free-style predicate-like parsed expression. */
    bool loadExpr(const ELogExpression* expr) final;

    /**
     * @brief Filters a log record.
     * @param logRecord The log record to filter.
     * @return true If the log record is to be logged.
     * @return false If the log record is to be discarded.
     */
    bool filterLogRecord(const ELogRecord& logRecord) final;

private:
    std::string m_threadName;

    ELOG_DECLARE_FILTER(ELogThreadNameFilter, thread_name);
};

class ELOG_API ELogSourceFilter : public ELogCmpFilter {
public:
    ELogSourceFilter(const char* logSourceName = "", ELogCmpOp cmpOp = ELogCmpOp::CMP_OP_EQ)
        : ELogCmpFilter(cmpOp), m_logSourceName(logSourceName) {}
    ELogSourceFilter(const ELogSourceFilter&) = delete;
    ELogSourceFilter(ELogSourceFilter&&) = delete;
    ELogSourceFilter& operator=(const ELogSourceFilter&) = delete;
    ~ELogSourceFilter() final {}

    /** @brief Loads filter from configuration. */
    bool load(const ELogConfigMapNode* filterCfg) final;

    /** @brief Loads filter from a free-style predicate-like parsed expression. */
    bool loadExpr(const ELogExpression* expr) final;

    /**
     * @brief Filters a log record.
     * @param logRecord The log record to filter.
     * @return true If the log record is to be logged.
     * @return false If the log record is to be discarded.
     */
    bool filterLogRecord(const ELogRecord& logRecord) final;

private:
    std::string m_logSourceName;

    ELOG_DECLARE_FILTER(ELogSourceFilter, log_source);
};

class ELOG_API ELogModuleFilter : public ELogCmpFilter {
public:
    ELogModuleFilter(const char* logModuleName = "", ELogCmpOp cmpOp = ELogCmpOp::CMP_OP_EQ)
        : ELogCmpFilter(cmpOp), m_logModuleName(logModuleName) {}
    ELogModuleFilter(const ELogModuleFilter&) = delete;
    ELogModuleFilter(ELogModuleFilter&&) = delete;
    ELogModuleFilter& operator=(const ELogModuleFilter&) = delete;
    ~ELogModuleFilter() final {}

    /** @brief Loads filter from configuration. */
    bool load(const ELogConfigMapNode* filterCfg) final;

    /** @brief Loads filter from a free-style predicate-like parsed expression. */
    bool loadExpr(const ELogExpression* expr) final;

    /**
     * @brief Filters a log record.
     * @param logRecord The log record to filter.
     * @return true If the log record is to be logged.
     * @return false If the log record is to be discarded.
     */
    bool filterLogRecord(const ELogRecord& logRecord) final;

private:
    std::string m_logModuleName;

    ELOG_DECLARE_FILTER(ELogModuleFilter, log_module);
};

class ELOG_API ELogFileNameFilter : public ELogCmpFilter {
public:
    ELogFileNameFilter(const char* fileName = "", ELogCmpOp cmpOp = ELogCmpOp::CMP_OP_EQ)
        : ELogCmpFilter(cmpOp), m_fileName(fileName) {}
    ELogFileNameFilter(const ELogFileNameFilter&) = delete;
    ELogFileNameFilter(ELogFileNameFilter&&) = delete;
    ELogFileNameFilter& operator=(const ELogFileNameFilter&) = delete;
    ~ELogFileNameFilter() final {}

    /** @brief Loads filter from configuration. */
    bool load(const ELogConfigMapNode* filterCfg) final;

    /** @brief Loads filter from a free-style predicate-like parsed expression. */
    bool loadExpr(const ELogExpression* expr) final;

    /**
     * @brief Filters a log record.
     * @param logRecord The log record to filter.
     * @return true If the log record is to be logged.
     * @return false If the log record is to be discarded.
     * @note File name field may contain relative path, and searched path may be just bare name, so
     * it is advised to use CONTAINS operator for this filter.
     */
    bool filterLogRecord(const ELogRecord& logRecord) final;

private:
    std::string m_fileName;

    ELOG_DECLARE_FILTER(ELogFileNameFilter, file_name);
};

class ELOG_API ELogLineNumberFilter : public ELogCmpFilter {
public:
    ELogLineNumberFilter(int lineNumber = 0, ELogCmpOp cmpOp = ELogCmpOp::CMP_OP_EQ)
        : ELogCmpFilter(cmpOp), m_lineNumber(lineNumber) {}
    ELogLineNumberFilter(const ELogLineNumberFilter&) = delete;
    ELogLineNumberFilter(ELogLineNumberFilter&&) = delete;
    ELogLineNumberFilter& operator=(const ELogLineNumberFilter&) = delete;
    ~ELogLineNumberFilter() final {}

    /** @brief Loads filter from configuration. */
    bool load(const ELogConfigMapNode* filterCfg) final;

    /** @brief Loads filter from a free-style predicate-like parsed expression. */
    bool loadExpr(const ELogExpression* expr) final;

    /**
     * @brief Filters a log record.
     * @param logRecord The log record to filter.
     * @return true If the log record is to be logged.
     * @return false If the log record is to be discarded.
     */
    bool filterLogRecord(const ELogRecord& logRecord) final;

private:
    int m_lineNumber;

    ELOG_DECLARE_FILTER(ELogLineNumberFilter, line_number);
};

class ELOG_API ELogFunctionNameFilter : public ELogCmpFilter {
public:
    ELogFunctionNameFilter(const char* functionName = "", ELogCmpOp cmpOp = ELogCmpOp::CMP_OP_EQ)
        : ELogCmpFilter(cmpOp), m_functionName(functionName) {}
    ELogFunctionNameFilter(const ELogFunctionNameFilter&) = delete;
    ELogFunctionNameFilter(ELogFunctionNameFilter&&) = delete;
    ELogFunctionNameFilter& operator=(const ELogFunctionNameFilter&) = delete;
    ~ELogFunctionNameFilter() final {}

    /** @brief Loads filter from configuration. */
    bool load(const ELogConfigMapNode* filterCfg) final;

    /** @brief Loads filter from a free-style predicate-like parsed expression. */
    bool loadExpr(const ELogExpression* expr) final;

    /**
     * @brief Filters a log record.
     * @param logRecord The log record to filter.
     * @return true If the log record is to be logged.
     * @return false If the log record is to be discarded.
     * @note Function field can be very noisy, having parameter qualified type names and such, so it
     * is advised to use CONTAINS operator for this filter.
     */
    bool filterLogRecord(const ELogRecord& logRecord) final;

private:
    std::string m_functionName;

    ELOG_DECLARE_FILTER(ELogFunctionNameFilter, function_name);
};

class ELOG_API ELogLevelFilter : public ELogCmpFilter {
public:
    ELogLevelFilter(ELogLevel logLevel = ELEVEL_INFO, ELogCmpOp cmpOp = ELogCmpOp::CMP_OP_LE)
        : ELogCmpFilter(cmpOp), m_logLevel(logLevel) {}
    ELogLevelFilter(const ELogLevelFilter&) = delete;
    ELogLevelFilter(ELogLevelFilter&&) = delete;
    ELogLevelFilter& operator=(const ELogLevelFilter&) = delete;
    ~ELogLevelFilter() final {}

    /** @brief Loads filter from configuration. */
    bool load(const ELogConfigMapNode* filterCfg) final;

    /** @brief Loads filter from a free-style predicate-like parsed expression. */
    bool loadExpr(const ELogExpression* expr) final;

    /**
     * @brief Filters a log record.
     * @param logRecord The log record to filter.
     * @return true If the log record is to be logged.
     * @return false If the log record is to be discarded.
     */
    bool filterLogRecord(const ELogRecord& logRecord) final;

private:
    ELogLevel m_logLevel;

    ELOG_DECLARE_FILTER(ELogLevelFilter, log_level);
};

class ELOG_API ELogMsgFilter : public ELogCmpFilter {
public:
    ELogMsgFilter(const char* logMsg = "", ELogCmpOp cmpOp = ELogCmpOp::CMP_OP_EQ)
        : ELogCmpFilter(cmpOp), m_logMsg(logMsg) {}
    ELogMsgFilter(const ELogMsgFilter&) = delete;
    ELogMsgFilter(ELogMsgFilter&&) = delete;
    ELogMsgFilter& operator=(const ELogMsgFilter&) = delete;
    ~ELogMsgFilter() final {}

    /** @brief Loads filter from configuration. */
    bool load(const ELogConfigMapNode* filterCfg) final;

    /** @brief Loads filter from a free-style predicate-like parsed expression. */
    bool loadExpr(const ELogExpression* expr) final;

    /**
     * @brief Filters a log record.
     * @param logRecord The log record to filter.
     * @return true If the log record is to be logged.
     * @return false If the log record is to be discarded.
     */
    bool filterLogRecord(const ELogRecord& logRecord) final;

private:
    std::string m_logMsg;

    ELOG_DECLARE_FILTER(ELogMsgFilter, log_msg);
};

class ELOG_API ELogCountFilter : public ELogCmpFilter {
public:
    ELogCountFilter(uint64_t count = 0)
        : ELogCmpFilter(ELogCmpOp::CMP_OP_EQ), m_runningCounter(0), m_count(count) {}
    ELogCountFilter(const ELogCountFilter&) = delete;
    ELogCountFilter(ELogCountFilter&&) = delete;
    ELogCountFilter& operator=(const ELogCountFilter&) = delete;
    ~ELogCountFilter() final {}

    /** @brief Loads filter from configuration. */
    bool load(const ELogConfigMapNode* filterCfg) final;

    /** @brief Loads filter from a free-style predicate-like parsed expression. */
    bool loadExpr(const ELogExpression* expr) final;

    /**
     * @brief Filters a log record.
     * @param logRecord The log record to filter.
     * @return true If the log record is to be logged.
     * @return false If the log record is to be discarded.
     */
    bool filterLogRecord(const ELogRecord& logRecord) final;

private:
    std::atomic<uint64_t> m_runningCounter;
    uint64_t m_count;

    ELOG_DECLARE_FILTER(ELogCountFilter, count);
};

}  // namespace elog

#endif  // __ELOG_FILTER_H__