#ifndef __ELOG_LOGGER_H__
#define __ELOG_LOGGER_H__

#include <atomic>
#include <cstdarg>

#include "elog_level.h"
#include "elog_record_builder.h"
#include "elog_source.h"

namespace elog {

class ELOG_API ELogLogger {
public:
    /** @ref Destructor. */
    virtual ~ELogLogger() {}

    /**
     * @brief Formats a log message and sends it to all log target.
     * @param logLevel The log level. No log level checking takes place.
     * @param file The issuing file name.
     * @param line The issuing line.
     * @param function The issuing function.
     * @param fmt The message format.
     * @param ... The message arguments.
     */
    void logFormat(ELogLevel logLevel, const char* file, int line, const char* function,
                   const char* fmt, ...);

    /**
     * @brief Formats a log message and sends it to all log target.
     * @param logLevel The log level. No log level checking takes place.
     * @param file The issuing file name.
     * @param line The issuing line.
     * @param function The issuing function.
     * @param fmt The message format.
     * @param args The message arguments.
     */
    void logFormatV(ELogLevel logLevel, const char* file, int line, const char* function,
                    const char* fmt, va_list args);

    /**
     * @brief Sends unformatted log message to all log targets.
     * @param logLevel The log level. No log level checking takes place.
     * @param file The issuing file name.
     * @param line The issuing line.
     * @param function The issuing function.
     * @param msg The log message.
     */
    void logNoFormat(ELogLevel logLevel, const char* file, int line, const char* function,
                     const char* msg);

    /**
     * @brief Starts a multi-part log message.
     * @param logLevel The log level. No log level checking takes place.
     * @param file The issuing file name.
     * @param line The issuing line.
     * @param function The issuing function.
     * @param fmt The message format.
     * @param ... The message arguments.
     */
    void startLog(ELogLevel logLevel, const char* file, int line, const char* function,
                  const char* fmt, ...);

    /**
     * @brief Starts a multi-part log message (no formatting).
     * @param logLevel The log level. No log level checking takes place.
     * @param file The issuing file name.
     * @param line The issuing line.
     * @param function The issuing function.
     * @param msg The log message.
     */
    void startLogNoFormat(ELogLevel logLevel, const char* file, int line, const char* function,
                          const char* msg);

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
    inline void finishLog() { finishLog(getRecordBuilder()); }

    /** @brief Queries whether a multi-part log message is being constructed. */
    inline bool isLogging(const ELogRecordBuilder& recordBuilder) const {
        return recordBuilder.getOffset() > 0;
    }

    /** @brief Queries whether the logger can issue log message with the given level. */
    inline bool canLog(ELogLevel logLevel) const { return m_logSource->canLog(logLevel); }

    /** @brief Retrieves the controlling log source. */
    inline ELogSource* getLogSource() { return m_logSource; }

    /** @brief Retrieves the controlling log source. */
    inline ELogSource* getLogSource() const { return m_logSource; }

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

    /** @brief Push current builder on builder stack and open a new builder. */
    virtual void pushRecordBuilder() = 0;

    /** @brief Pop current builder from builder stack and restore previous builder. */
    virtual void popRecordBuilder() = 0;

private:
    /** @var The originating log source. */
    ELogSource* m_logSource;

    /**
     * @brief Starts a multi-part log message.
     * @param logLevel The log level. No log level checking takes place.
     */
    void startLogRecord(ELogRecord& logRecord, ELogLevel logLevel, const char* file, int line,
                        const char* function);

    void finishLog(ELogRecordBuilder& recordBuilder);

    inline void appendMsgV(ELogRecordBuilder& recordBuilder, const char* fmt, va_list ap) {
        va_list apCopy;
        va_copy(apCopy, ap);
        (void)recordBuilder.appendV(fmt, ap);
        va_end(apCopy);
    }

    inline void appendMsg(ELogRecordBuilder& recordBuilder, const char* msg) {
        (void)recordBuilder.append(msg);
    }
};

}  // namespace elog

#endif  // __ELOG_LOGGER_H__