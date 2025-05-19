#ifndef __ELOG_TARGET_H__
#define __ELOG_TARGET_H__

#include <atomic>
#include <string>
#include <vector>

#include "elog_record.h"

namespace elog {

class ELOG_API ELogFilter;
class ELOG_API ELogFormatter;
class ELOG_API ELogFlushPolicy;

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

    inline const char* getTypeName() const { return m_typeName.c_str(); }

    /** @brief Order the log target to start (required for threaded targets). */
    bool start();

    /** @brief Order the log target to stop (required for threaded targets). */
    bool stop();

    /** @brief Sends a log record to a log target. */
    virtual void log(const ELogRecord& logRecord);

    /** @brief Orders a buffered log target to flush it log messages. */
    virtual void flush() = 0;

    /** @brief Sets optional log target name. */
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

    /** @brief As log target may be chained as in a list, this retrieves the final log target. */
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
          m_logLevel(ELEVEL_DIAG),
          m_logFilter(nullptr),
          m_logFormatter(nullptr),
          m_flushPolicy(flushPolicy),
          m_addNewLine(false),
          m_bytesWritten(0) {}

    /** @brief Order the log target to start (required for threaded targets). */
    virtual bool startLogTarget() = 0;

    /** @brief Order the log target to stop (required for threaded targets). */
    virtual bool stopLogTarget() = 0;

    bool shouldLog(const ELogRecord& logRecord);

    void formatLogMsg(const ELogRecord& logRecord, std::string& logMsg);

    virtual void logFormattedMsg(const std::string& logMsg) {}

    bool shouldFlush(const std::string& logMsg);

    inline void addBytesWritten(uint64_t bytes) {
        m_bytesWritten.fetch_add(bytes, std::memory_order_relaxed);
    }

private:
    std::string m_typeName;
    std::string m_name;
    ELogLevel m_logLevel;
    ELogFilter* m_logFilter;
    ELogFormatter* m_logFormatter;
    ELogFlushPolicy* m_flushPolicy;
    bool m_addNewLine;
    std::atomic<uint64_t> m_bytesWritten;
};

/** @class Combined log target. Dispatches to multiple log targets. */
class ELOG_API ELogCombinedTarget : public ELogTarget {
public:
    ELogCombinedTarget() : ELogTarget("combined") {}
    ~ELogCombinedTarget() final {}

    inline void addLogTarget(ELogTarget* target) { m_logTargets.push_back(target); }

    /** @brief Sends a log record to a log target. */
    void log(const ELogRecord& logRecord) final;

    /** @brief Orders a buffered log target to flush it log messages. */
    void flush() final;

protected:
    /** @brief Order the log target to start (required for threaded targets). */
    bool startLogTarget() final;

    /** @brief Order the log target to stop (required for threaded targets). */
    bool stopLogTarget() final;

private:
    std::vector<ELogTarget*> m_logTargets;
};

}  // namespace elog

#endif  // __ELOG_TARGET_H__