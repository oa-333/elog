#ifndef __ELOG_TARGET_H__
#define __ELOG_TARGET_H__

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "elog_buffer.h"
#include "elog_common_def.h"
#include "elog_flush_policy.h"
#include "elog_record.h"

namespace elog {

class ELOG_API ELogFilter;
class ELOG_API ELogFormatter;

// add statistics reporting API so the full pipeline status can be tracked in real time:
// messages submitted
// messages collected
// messages written
// bytes submitted
// bytes collected
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

    /** @brief Allocate thread local storage key for per-thread log buffer. */
    static bool createLogBufferKey();

    /** @brief Free thread local storage key used for per-thread log buffer. */
    static bool destroyLogBufferKey();

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
        m_requiresLock = 0;
        onThreadSafe();
    }

    /** @brief Order the log target to start (required for threaded targets). */
    bool start();

    /** @brief Order the log target to stop (required for threaded targets). */
    bool stop();

    /** @brief Sends a log record to a log target. */
    void log(const ELogRecord& logRecord);

    /** @brief Orders a buffered log target to flush it log messages. */
    void flush();

    /** @brief Sets the log target id. */
    inline void setId(ELogTargetId id) { m_id = id; }

    /** @brief Retrieves the log target id. */
    inline ELogTargetId getId() { return m_id; }

    /** @brief Sets a pass key to the target. */
    inline void setPassKey() { m_passKey = generatePassKey(); }

    inline ELogPassKey getPassKey() const { return m_passKey; }

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
    void setAddNewLine(bool addNewLine) { m_addNewLine = addNewLine ? 1 : 0; }

    /**
     * @brief Sets the flush policy for the log target. Derived classes should take into
     * consideration the configured flush policy and override global policy configuration. If no
     * flush policy is set the the log target will not be flushed at all, which is ok in some
     * situations (e.g. buffered file already takes care by itself of occasional flush).
     */
    void setFlushPolicy(ELogFlushPolicy* flushPolicy);

    /** @brief Retrieve the installed flush policy. */
    inline ELogFlushPolicy* getFlushPolicy() { return m_flushPolicy; }

    /**
     * @brief Detaches the log target from its flush-policy/fiter/formatter without deleting them.
     */
    inline void detach() {
        m_flushPolicy = nullptr;
        m_logFilter = nullptr;
        m_logFormatter = nullptr;
    }

    /** @brief As log target may be chained as in a list. This retrieves the final log target.
     */
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
          m_id(ELOG_INVALID_TARGET_ID),
          m_passKey(ELOG_NO_PASSKEY),
          m_logLevel(ELEVEL_DIAG),
          m_isRunning(false),
          m_isNativelyThreadSafe(false),
          m_isExternallyThreadSafe(false),
          m_addNewLine(0),
          m_requiresLock(1),
          m_logFilter(nullptr),
          m_logFormatter(nullptr),
          m_flushPolicy(flushPolicy),
          m_bytesWritten(0) {}

    /** @brief Sets the natively-thread-safe property to true. */
    inline void setNativelyThreadSafe() {
        m_isNativelyThreadSafe = true;
        m_requiresLock = 0;
        onThreadSafe();
    }

    /** @brief Queries whether the log target is exected in a thread safe environment. */
    inline bool isExternallyThreadSafe() const { return m_isExternallyThreadSafe; }

    /**
     * @brief Notifies the log target that it has turned thread-safe. Derived class may take
     * special measures.
     */
    virtual void onThreadSafe() {}

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

    /** @brief Helper method for formatting a log message. */
    void formatLogBuffer(const ELogRecord& logRecord, ELogBuffer& logBuffer);

    /** @brief If not overriding @ref writeLogRecord(), then this method must be implemented. */
    virtual void logFormattedMsg(const char* formattedLogMsg, size_t length) {}

    /** @brief Helper method for querying whether the log record can be written to log. */
    bool canLog(const ELogRecord& logRecord);

private:
    std::string m_typeName;
    std::string m_name;
    ELogTargetId m_id;
    ELogPassKey m_passKey;
    ELogLevel m_logLevel;
    std::atomic<bool> m_isRunning;
    bool m_isNativelyThreadSafe;
    bool m_isExternallyThreadSafe;
    alignas(8) uint64_t m_addNewLine;
    // this member is pretty hot, so we avoid using 1 byte boolean, and use instead aligned uint64
    alignas(8) uint64_t m_requiresLock;
    ELogFilter* m_logFilter;
    ELogFormatter* m_logFormatter;
    ELogFlushPolicy* m_flushPolicy;
    std::atomic<uint64_t> m_bytesWritten;
    std::recursive_mutex m_lock;

    bool startNoLock();
    bool stopNoLock();
    void logNoLock(const ELogRecord& logRecord);

    /** @brief Helper method for querying whether the log target should be flushed. */
    inline bool shouldFlush(uint32_t bytesWritten) {
        return m_flushPolicy != nullptr && m_flushPolicy->shouldFlush(bytesWritten);
    }

    /** @brief Helper method for reporting bytes written to log target. */
    inline void addBytesWritten(uint64_t bytes) {
        m_bytesWritten.fetch_add(bytes, std::memory_order_relaxed);
    }

    ELogPassKey generatePassKey();
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