#ifndef __ELOG_LOGGER_H__
#define __ELOG_LOGGER_H__

#include <atomic>
#include <cstdarg>

#include "elog_common_def.h"
#include "elog_level.h"
#include "elog_record_builder.h"
#include "elog_source.h"
#include "elog_type_codec.h"

namespace elog {

class ELOG_API ELogLogger {
public:
    /** @ref Destructor. */
    virtual ~ELogLogger() {}

    /**
     * @brief Formats a log message and sends it to all log
     * target.
     * @param logLevel The log level. No log level checking takes
     * place.
     * @param file The issuing file name.
     * @param line The issuing line.
     * @param function The issuing function.
     * @param fmt The message format.
     * @param ... The message arguments.
     */
    void logFormat(ELogLevel logLevel, const char* file, int line, const char* function,
                   const char* fmt, ...);

    /**
     * @brief Formats a log message and sends it to all log
     * target.
     * @param logLevel The log level. No log level checking takes
     * place.
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
     * @param logLevel The log level. No log level checking takes
     * place.
     * @param file The issuing file name.
     * @param line The issuing line.
     * @param function The issuing function.
     * @param msg The log message.
     */
    void logNoFormat(ELogLevel logLevel, const char* file, int line, const char* function,
                     const char* msg);

    /**
     * @brief Starts a multi-part log message.
     * @param logLevel The log level. No log level checking takes
     * place.
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
     * @param logLevel The log level. No log level checking takes
     * place.
     * @param file The issuing file name.
     * @param line The issuing line.
     * @param function The issuing function.
     * @param msg The log message.
     */
    void startLogNoFormat(ELogLevel logLevel, const char* file, int line, const char* function,
                          const char* msg);

    /**
     * @brief Appends formatted message to a multi-part log
     * message.
     * @param fmt The message format.
     * @param ... The message arguments.
     */
    void appendLog(const char* fmt, ...);

    /**
     * @brief Appends unformatted message to a multi-part log
     * message.
     * @param msg The log message.
     */
    void appendLogNoFormat(const char* msg);

    /** @brief Terminates a multi-part log message and sends it
     * to all log targets. */
    inline void finishLog() { finishLog(getRecordBuilder()); }

    inline void discardLog() { discardLog(getRecordBuilder()); }

    /** @brief Queries whether a multi-part log message is being
     * constructed. */
    inline bool isLogging(const ELogRecordBuilder* recordBuilder) const {
        return recordBuilder->getOffset() > 0;
    }

    /** @brief Queries whether the logger can issue log message
     * with the given level. */
    inline bool canLog(ELogLevel logLevel) const {
        // for the sake of pre-init logger we allow all log
        // messages to be logged in case log source is null
        return m_logSource == nullptr ? true : m_logSource->canLog(logLevel);
    }

    /** @brief Retrieves the controlling log source. */
    inline ELogSource* getLogSource() { return m_logSource; }

    /** @brief Retrieves the controlling log source. */
    inline ELogSource* getLogSource() const { return m_logSource; }

#ifdef ELOG_ENABLE_FMT_LIB
    /** @brief Logs a binary log record. */
    template <typename... Args>
    void logBinary(ELogLevel logLevel, const char* file, int line, const char* function,
                   const char* fmt, Args... args) {
        // NOTE: if something goes wrong we must discard log message, or else we may crash due to
        // invalid format string parameters
        ELogRecordBuilder* recordBuilder =
            startBinaryLogRecord(logLevel, file, line, function, ELOG_RECORD_BINARY);
        if (recordBuilder == nullptr) {
            return;
        }

        // append early format string, instead of passing it
        // around in variadic template NOTE: we must append
        // terminating null as well, for deserialization
        bool res = false;
        if (recordBuilder->append(fmt, strlen(fmt) + 1)) {
            if (encodeMsg(recordBuilder, 0, args...)) {
                finishLog(recordBuilder);
                res = true;
            }
        }
        if (!res) {
            discardLog(recordBuilder);
        }
    }

    /** @brief Logs a binary log record. */
    template <typename... Args>
    void logBinaryCached(ELogLevel logLevel, const char* file, int line, const char* function,
                         ELogCacheEntryId cacheEntryId, Args... args) {
        // NOTE: if something goes wrong we must discard log message, or else we may crash due to
        // invalid format string parameters
        ELogRecordBuilder* recordBuilder = startBinaryLogRecord(
            logLevel, file, line, function, ELOG_RECORD_BINARY | ELOG_RECORD_FMT_CACHED);
        if (recordBuilder == nullptr) {
            return;
        }

        // append early cache entry id, instead of passing it
        // around in variadic template NOTE: we must append
        // terminating null as well, for deserialization
        bool res = false;
        if (recordBuilder->appendData(cacheEntryId)) {
            if (encodeMsg(recordBuilder, 0, args...)) {
                finishLog(recordBuilder);
                res = true;
            }
        }
        if (!res) {
            discardLog(recordBuilder);
        }
    }

    /** @brief Resolves a binary log record, putting the resolved
     * message into a log buffer. */
    static bool resolveLogRecord(const ELogRecord& logRecord, ELogBuffer& logBuffer);
#endif

protected:
    /**
     * @brief Constructor
     * @param logSource The originating log source.
     */
    ELogLogger(ELogSource* logSource) : m_logSource(logSource) {}

    ELogLogger(const ELogLogger&) = delete;
    ELogLogger(ELogLogger&&) = delete;
    ELogLogger& operator=(const ELogLogger&) = delete;

    /** @brief Retrieves the underlying log record builder. */
    virtual ELogRecordBuilder* getRecordBuilder() = 0;

    /** @brief Retrieves the underlying log record builder. */
    virtual const ELogRecordBuilder* getRecordBuilder() const = 0;

    /** @brief Push current builder on builder stack and open a
     * new builder. */
    virtual ELogRecordBuilder* pushRecordBuilder() = 0;

    /** @brief Pop current builder from builder stack and restore
     * previous builder. */
    virtual void popRecordBuilder() = 0;

    /** @brief Finish logging (default behavior: finalize formatting and send to log target). */
    virtual void finishLog(ELogRecordBuilder* recordBuilder);

private:
    /** @var The originating log source. */
    ELogSource* m_logSource;

    /**
     * @brief Starts a multi-part log message.
     * @param logLevel The log level. No log level checking takes
     * place.
     */
    void startLogRecord(ELogRecord& logRecord, ELogLevel logLevel, const char* file, int line,
                        const char* function, uint8_t flags = ELOG_RECORD_FORMATTED);

    ELogRecordBuilder* startBinaryLogRecord(ELogLevel logLevel, const char* file, int line,
                                            const char* function,
                                            uint8_t flags = ELOG_RECORD_FORMATTED);

    inline void appendMsgV(ELogRecordBuilder* recordBuilder, const char* fmt, va_list args) {
        va_list argsCopy;
        va_copy(argsCopy, args);
        (void)recordBuilder->appendV(fmt, args);
        va_end(argsCopy);
    }

    inline void appendMsg(ELogRecordBuilder* recordBuilder, const char* msg) {
        (void)recordBuilder->append(msg);
    }

    /** @brief Discards logging. */
    void discardLog(ELogRecordBuilder* recordBuilder);

#ifdef ELOG_ENABLE_FMT_LIB
    template <typename T, typename... Args>
    bool encodeMsg(ELogRecordBuilder* recordBuilder, uint64_t paramCount, T arg, Args... args) {
        if (!recordBuilder->appendData(getTypeCode<T>())) {
            return false;
        }
        if (!encodeType<T>(arg, recordBuilder->getBuffer())) {
            return false;
        }
        return encodeMsg(recordBuilder, paramCount + 1, args...);
    }

    bool encodeMsg(ELogRecordBuilder* recordBuilder, uint64_t paramCount) {
        return recordBuilder->appendDataAt((uint8_t)paramCount, 0);
    }
#endif
};

}  // namespace elog

#endif  // __ELOG_LOGGER_H__