#ifndef __ELOG_TARGET_H__
#define __ELOG_TARGET_H__

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "elog_record.h"

namespace elog {

class ELOG_API ELogFilter;
class ELOG_API ELogFormatter;
class ELOG_API ELogFlushPolicy;

// TODO: VERY IMPORTNAT: define clearly semantics of thread safety when using log target.
// currently it is inconsistent. file log target is thread safe due to use of fwrite/fflush etc.
// but other log writers (e.g. Kafka, PostgreSQL, gRPC) are NOT thread safe, and normally should be
// put behind a queue
// The correct way to do this is to have the log target DECLARE whether it is natively thread-safe
// or not, so that the caller may be able to tell dynamically whether a lock is required.
// In addition, the user should be able to order the log target to take measures against concurrent
// access.
// Another important point is that with active flush policy (i.e. ones that have a background thread
// that calls flush, just as in timed flush policy), the call to ELogTarget::flush() is BY
// DEFINITION not thread safe, unless the log target is by nature thread safe.
// so the proposed API is:
// virtual bool isNativelyThreadSafe() { return false; } //  by default NOT
// virtual void enforceThreadSafeSemantics();  // default implementation uses a mutex
// put this in a separate commit, since it may affect performance
// BY DEFAULT, if the log target is not natively thread safe, then enforce thread safety should be
// used, without any user configuration, so user actually only needs to configure if they can assure
// thread-safe log target access, such as in the case of asynchronous log target.
// this behavior is enforced by introducing mandatory constructor parameter to ELogTarget,
// specifying whether the log target is natively thread-safe.
// ALSO interface methods log/flush cannot be virtual, so we can enforce thread-safety

// add statistics reporting API:
// messages submitted
// messages written
// bytes submitted
// bytes written

// this way when messages submitted == written we know that the log target is "caught-up" (need a
// better name), but this logic can now be employed by the client, since we cannot tell when it is
// really caught up unless we now the entire amount of expected log messages.

/**
 * @class Parent class for all log targets. Used to decouple log formatting from actual logging.
 * Possible log targets could be:
 * - Log file (possibly segmented)
 * - External logging system (database, or adapter to containing application)
 * - Message Queue of some message broker system
 * - Deferring schemes (actual logging takes place in a different thread)
 */
class ELOG_API ELogTarget {
public:
    virtual ~ELogTarget() {
        setLogFilter(nullptr);
        setLogFormatter(nullptr);
        setFlushPolicy(nullptr);
    }

    /** @brief Retrieves the unique type name of the log target. */
    inline const char* getTypeName() const { return m_typeName.c_str(); }

    /**
     * @brief Queries whether the log target is by nature thread safe. If an implementation already
     * takes measures against concurrent access (or alternatively, it uses some third party library
     * that takes care of concurrency issues), then it is said to be natively thread safe.
     */
    inline bool isNativelyThreadSafe() { return m_isNativelyThreadSafe; }

    /**
     * @brief Informs the log target it does not need to take care of concurrency issue, as
     * external log target access is guaranteed to be thread-safe.
     */
    inline void setExternallyThreadSafe() {
        m_isExternallyThreadSafe = true;
        m_requiresLock = false;
    }

    /** @brief Order the log target to start (required for threaded targets). */
    bool start();

    /** @brief Order the log target to stop (required for threaded targets). */
    bool stop();

    /** @brief Sends a log record to a log target. */
    void log(const ELogRecord& logRecord);

    /** @brief Orders a buffered log target to flush it log messages. */
    void flush();

    /**
     * @brief Sets optional log target name (for identification, can be used when searching for a
     * log target by name, see @ref ELogSystem::getLogTarget()).
     */
    inline void setName(const char* name) { m_name = name; }

    /** @brief Retrieves optional log target name. */
    inline const char* getName() const { return m_name.c_str(); }

    /**
     * @brief Sets the log level of the log target. Derived classes should take into consideration
     * this value, and filter out messages without high enough log level.
     */
    inline void setLogLevel(ELogLevel logLevel) { m_logLevel = logLevel; }

    /** @brief Retrieves the log level associated with this log target. */
    inline ELogLevel getLogLevel() const { return m_logLevel; }

    /**
     * @brief Sets the log filter for the log target. Derived classes should take into
     * consideration the configured filter and apply it in addition to the global filter
     * configuration.
     */
    void setLogFilter(ELogFilter* logFilter);

    /** @brief Retrieves the log filter associated with this log target. */
    inline ELogFilter* getLogFilter() { return m_logFilter; }

    /**
     * @brief Sets the log formatter for the log target. Derived classes should take into
     * consideration the configured formatter and override global formatter configuration.
     */
    void setLogFormatter(ELogFormatter* logFormatter);

    /** @brief Retrieves the log formatter associated with this log target. */
    inline ELogFormatter* getLogFormatter() { return m_logFormatter; }

    /**
     * @brief Configures whether to add a new line character at the end of the formatted message.
     * Typically file log targets will add a new line, while others, such as db log targets, will
     * not need an additional new line at the end of the formatted message.
     */
    void setAddNewLine(bool addNewLine) { m_addNewLine = addNewLine; }

    /**
     * @brief Sets the flush policy for the log target. Derived classes should take into
     * consideration the configured flush policy and override global policy configuration.
     */
    void setFlushPolicy(ELogFlushPolicy* flushPolicy);

    /** @brief As log target may be chained as in a list. This retrieves the final log target. */
    virtual ELogTarget* getEndLogTarget() { return this; }

    /**
     * @brief Retrieves the number of bytes written to this log target. In case of a compound log
     * target, this call retrieves the number recorded in the last log target.
     */
    inline uint64_t getBytesWritten() {
        return getEndLogTarget()->m_bytesWritten.load(std::memory_order_relaxed);
    }

    /** @brief Queries whether the log target has written all pending messages. */
    virtual bool isCaughtUp(uint64_t& writeCount, uint64_t& readCount) { return true; }

protected:
    // NOTE: setting log level to DIAG by default has the effect of no log level limitation on the
    // target
    ELogTarget(const char* typeName, ELogFlushPolicy* flushPolicy = nullptr)
        : m_typeName(typeName),
          m_isNativelyThreadSafe(false),
          m_isExternallyThreadSafe(false),
          m_requiresLock(true),
          m_logLevel(ELEVEL_DIAG),
          m_logFilter(nullptr),
          m_logFormatter(nullptr),
          m_flushPolicy(flushPolicy),
          m_addNewLine(false),
          m_bytesWritten(0) {}

    /** @brief Sets the natively-thread-safe property to true. */
    inline void setNativelyThreadSafe() {
        m_isNativelyThreadSafe = true;
        m_requiresLock = false;
    }

    /** @brief Queries whether the log target is exected in a thread safe environment. */
    inline bool isExternallyThreadSafe() const { return m_isExternallyThreadSafe; }

    /** @brief Order the log target to start (thread-safe). */
    virtual bool startLogTarget() = 0;

    /** @brief Order the log target to stop (thread-safe). */
    virtual bool stopLogTarget() = 0;

    /**
     * @brief Order the log target to write a log record (thread-safe).
     * @return The number of bytes written to log.
     */
    virtual uint32_t writeLogRecord(const ELogRecord& logRecord);

    /** @brief Order the log target to flush. */
    virtual void flushLogTarget() = 0;

    /** @brief Helper method for formatting a log message. */
    void formatLogMsg(const ELogRecord& logRecord, std::string& logMsg);

    /** @brief If not overriding @ref writeLogRecord(), then this method must be implemented. */
    virtual void logFormattedMsg(const std::string& logMsg) {}

private:
    std::string m_typeName;
    std::string m_name;
    bool m_isNativelyThreadSafe;
    bool m_isExternallyThreadSafe;
    bool m_requiresLock;
    std::recursive_mutex m_lock;
    ELogLevel m_logLevel;
    ELogFilter* m_logFilter;
    ELogFormatter* m_logFormatter;
    ELogFlushPolicy* m_flushPolicy;
    bool m_addNewLine;
    std::atomic<uint64_t> m_bytesWritten;

    bool startNoLock();
    bool stopNoLock();
    void logNoLock(const ELogRecord& logRecord);

    /** @brief Helper method for querying whether the log record should be written to log. */
    bool shouldLog(const ELogRecord& logRecord);

    /** @brief Helper method for querying whether the log target should be flushed. */
    bool shouldFlush(uint32_t bytesWritten);

    /** @brief Helper method for reporting bytes written to log target. */
    inline void addBytesWritten(uint64_t bytes) {
        m_bytesWritten.fetch_add(bytes, std::memory_order_relaxed);
    }
};

/** @class Combined log target. Dispatches to multiple log targets. */
class ELOG_API ELogCombinedTarget : public ELogTarget {
public:
    ELogCombinedTarget() : ELogTarget("combined") {}
    ~ELogCombinedTarget() final {}

    inline void addLogTarget(ELogTarget* target) { m_logTargets.push_back(target); }

protected:
    /** @brief Order the log target to start (required for threaded targets). */
    bool startLogTarget() final;

    /** @brief Order the log target to stop (required for threaded targets). */
    bool stopLogTarget() final;

    /** @brief Sends a log record to a log target. */
    uint32_t writeLogRecord(const ELogRecord& logRecord) final;

    /** @brief Orders a buffered log target to flush it log messages. */
    void flushLogTarget() final;

private:
    std::vector<ELogTarget*> m_logTargets;
};

}  // namespace elog

#endif  // __ELOG_TARGET_H__