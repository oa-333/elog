#ifndef __ELOG_LOGGER_H__
#define __ELOG_LOGGER_H__

#include <atomic>

#include "elog_def.h"
#include "elog_level.h"
#include "elog_record_builder.h"
#include "elog_source.h"

namespace elog {

class DLL_EXPORT ELogLogger {
public:
    /** @ref Destructor. */
    virtual ~ELogLogger() {}

    /**
     * @brief Formats a log message and sends it to all log target.
     * @param logLevel The log level. No log level checking takes place.
     * @param fmt The message format.
     * @param ... The message arguments.
     */
    void logFormat(ELogLevel logLevel, const char* fmt, ...);

    /**
     * @brief Sends unformatted log message to all log targets.
     * @param logLevel The log level. No log level checking takes place.
     * @param msg The log message.
     */
    void logNoFormat(ELogLevel logLevel, const char* msg);

    /**
     * @brief Starts a multi-part log message.
     * @param logLevel The log level. No log level checking takes place.
     * @param fmt The message format.
     * @param ... The message arguments.
     */
    void startLog(ELogLevel logLevel, const char* fmt, ...);

    /**
     * @brief Starts a multi-part log message (no formatting).
     * @param logLevel The log level. No log level checking takes place.
     * @param msg The log message.
     */
    void startLogNoFormat(ELogLevel logLevel, const char* msg);

    /**
     * @brief Appends formatted message to a multi-part log message.
     * @param fmt The message format.
     * @param ... The message arguments.
     */
    void appendLog(const char* fmt, ...);

    /**
     * @brief Appends unformatted message to a multi-part log message.
     * @param msg The log message.
     */
    void appendLogNoFormat(const char* msg);

    /** @brief Terminates a multi-part log message and sends it to all log targets. */
    void finishLog();

    /** @brief Queries whether a multi-part log message is being constructed. */
    bool isLogging() const;

    /** @brief Queries whether the logger can issue log message with the given level. */
    inline bool canLog(ELogLevel logLevel) const { return m_logSource->canLog(logLevel); }

protected:
    /**
     * @brief Constructor
     * @param logSource The originating log source.
     */
    ELogLogger(ELogSource* logSource) : m_logSource(logSource) {}

    /** @brief Retrieves the underlying log record builder. */
    virtual ELogRecordBuilder& getRecordBuilder() = 0;

    /** @brief Retrieves the underlying log record builder. */
    virtual const ELogRecordBuilder& getRecordBuilder() const = 0;

private:
    /** @var The originating log source. */
    ELogSource* m_logSource;

    /**
     * @brief Starts a multi-part log message.
     * @param logLevel The log level. No log level checking takes place.
     */
    void startLogRecord(ELogLevel logLevel);

    void appendMsgV(const char* fmt, va_list ap);

    void appendMsg(const char* msg);
};

}  // namespace elog

#endif  // __ELOG_LOGGER_H__