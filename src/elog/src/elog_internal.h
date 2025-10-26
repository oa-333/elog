#ifndef __ELOG_INTERNAL_H__
#define __ELOG_INTERNAL_H__

#include <string>

#include "elog_buffer.h"
#include "elog_common.h"
#include "elog_common_def.h"
#include "elog_def.h"
#include "elog_formatter.h"
#include "elog_params.h"
#include "elog_record.h"
#include "elog_source.h"

namespace elog {

/** @brief Retrieves a reference to the ELog's configured parameters. */
extern const ELogParams& getParams();

/** @brief Retrieves a reference to the ELog's configured parameters for modification purposes. */
extern ELogParams& modifyParams();

/** @brief Retrieves the maximum number of threads configured for ELog. */
extern uint32_t getMaxThreads();

/** @brief Resets the statistics counters for the current thread. */
extern void resetThreadStatCounters(uint64_t slotId);

/**
 * @brief Formats a log message, using the installed log formatter.
 * @param logRecord The log record to format.
 * @param[out] logMsg The resulting formatted log message.
 */
extern void formatLogMsg(const ELogRecord& logRecord, std::string& logMsg);

/**
 * @brief Formats a log message, using the installed log formatter.
 * @param logRecord The log record to format.
 * @param[out] logBuffer The resulting formatted log buffer.
 */
extern void formatLogBuffer(const ELogRecord& logRecord, ELogBuffer& logBuffer);

/** @brief Retrieves the default global log formatter. */
extern ELogFormatter* getDefaultLogFormatter();

/**
 * @brief Logs a log record. In essence to log record is sent to all registered log targets.
 * @param logRecord The lgo record to process.
 * @param logTargetAffinityMask Optionally restricts the message to be directed to a specific
 * log target.
 */
extern void logMsg(const ELogRecord& logRecord,
                   ELogTargetAffinityMask logTargetAffinityMask = ELOG_ALL_TARGET_AFFINITY_MASK);

#ifdef ELOG_ENABLE_LIFE_SIGN
/** @brief Writes a life-sign context record for the application's name. */
extern void reportAppNameLifeSign(const char* appName);

/** @brief Writes a life-sign context record for the current thread's name. */
extern void reportCurrentThreadNameLifeSign(elog_thread_id_t threadId, const char* threadName);
#endif

/** @brief Retrieve log sources matching a regular expression */
extern void getLogSources(const char* logSourceRegEx, std::vector<ELogSource*>& logSources);

/** @brief Retrieve log sources matching inclusion/exclusion regular expressions. */
extern void getLogSourcesEx(const char* includeRegEx, const char* excludeRegEx,
                            std::vector<ELogSource*>& logSources);

// log source visitor
class ELogSourceVisitor {
public:
    virtual ~ELogSourceVisitor() {}
    virtual void onLogSource(ELogSource* lgoSource) = 0;

protected:
    ELogSourceVisitor() {}
    ELogSourceVisitor(const ELogSourceVisitor&) = delete;
    ELogSourceVisitor(ELogSourceVisitor&&) = delete;
    ELogSourceVisitor& operator=(const ELogSourceVisitor&) = delete;
};

/** @brief Traverse all log sources. */
extern void visitLogSources(const char* includeRegEx, const char* excludeRegEx,
                            ELogSourceVisitor* visitor);

/** @brief Visit all log sources, possibly filtered by inclusion/exclusion regular expressions. */
template <typename F>
inline void forEachLogSource(const char* includeRegEx, const char* excludeRegEx, F f) {
    class Visitor : public ELogSourceVisitor {
    public:
        Visitor(F f) : m_f(f) {}
        Visitor() = delete;
        Visitor(const Visitor&) = delete;
        Visitor(Visitor&&) = delete;
        Visitor& operator=(const Visitor&) = delete;
        ~Visitor() {}

        void onLogSource(ELogSource* logSource) { m_f(logSource); }

    private:
        F m_f;
    };

    Visitor v(f);
    visitLogSources(includeRegEx, excludeRegEx, &v);
}

/** @brief Queries whether a time source is being used. */
inline bool isTimeSourceEnabled() {
    return getParams().m_enableTimeSource.m_atomicValue.load(std::memory_order_relaxed);
}

/** @brief Retrieves the current time from the time source. */
extern void getCurrentTimeFromSource(ELogTime& currentTime);

}  // namespace elog

#endif  // __ELOG_INTERNAL_H__