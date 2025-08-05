#ifndef __ELOG_STACK_TRACE_H__
#define __ELOG_STACK_TRACE_H__

#include "elog_def.h"

#ifdef ELOG_ENABLE_STACK_TRACE

#include "dbg_stack_trace.h"

namespace elog {

/** @brief Initializes the stack trace API. */
extern ELOG_API void initStackTrace();

/** @brief Retrieves the stack trace as resolved frame array. */
extern ELOG_API bool getStackTraceVector(dbgutil::StackTrace& stackTrace);

/** @brief Retrieves the stack trace as a string. */
extern ELOG_API bool getStackTraceString(std::string& stackTraceString);

class ELogStackEntryFilter : public dbgutil::StackEntryFilter {
public:
    ELogStackEntryFilter() {}
    ELogStackEntryFilter(const ELogStackEntryFilter&) = delete;
    ELogStackEntryFilter(ELogStackEntryFilter&&) = delete;
    ELogStackEntryFilter& operator=(const ELogStackEntryFilter&) = delete;
    ~ELogStackEntryFilter() final {}

    /**
     * @brief Filters a stack trace entry
     * @param stackEntry The stack entry.
     * @return true if the stack entry is to be processed, or false if should be skipped.
     */
    bool filterStackEntry(const dbgutil::StackEntry& stackEntry) final;
};

}  // namespace elog

#endif  // ELOG_ENABLE_STACK_TRACE

#endif  // __ELOG_STACK_TRACE_H__