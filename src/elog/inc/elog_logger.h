#ifndef __ELOG_LOGGER_H__
#define __ELOG_LOGGER_H__

#include <atomic>
#include <cstdarg>

#include "elog_common_def.h"
#include "elog_level.h"
#include "elog_record_builder.h"
#include "elog_source.h"

namespace elog {

// binary logging helper functions
#ifdef ELOG_ENABLE_FMT_LIB
/** @brief Generic template function for getting unique type encoding. */
template <typename T>
inline uint8_t encodeType() {
    return 0;
}

/** @def Macro for declaring type encoding */
#define ELOG_DECLARE_ENCODE_TYPE(type, value) \
    template <>                               \
    inline uint8_t encodeType<type>() {       \
        return value;                         \
    }

/** @def Special codes for primitive type. */
#define ELOG_UINT8_CODE ((uint8_t)0x01)
#define ELOG_UINT16_CODE ((uint8_t)0x02)
#define ELOG_UINT32_CODE ((uint8_t)0x03)
#define ELOG_UINT64_CODE ((uint8_t)0x04)
#define ELOG_INT8_CODE ((uint8_t)0x05)
#define ELOG_INT16_CODE ((uint8_t)0x06)
#define ELOG_INT32_CODE ((uint8_t)0x07)
#define ELOG_INT64_CODE ((uint8_t)0x08)
#define ELOG_FLOAT_CODE ((uint8_t)0x09)
#define ELOG_DOUBLE_CODE ((uint8_t)0x0A)
#define ELOG_BOOL_CODE ((uint8_t)0x0B)
#define ELOG_STRING_CODE ((uint8_t)0xF0)

// declare codes of primitive types
ELOG_DECLARE_ENCODE_TYPE(uint8_t, ELOG_UINT8_CODE)
ELOG_DECLARE_ENCODE_TYPE(uint16_t, ELOG_UINT16_CODE)
ELOG_DECLARE_ENCODE_TYPE(uint32_t, ELOG_UINT32_CODE)
ELOG_DECLARE_ENCODE_TYPE(uint64_t, ELOG_UINT64_CODE)
ELOG_DECLARE_ENCODE_TYPE(int8_t, ELOG_INT8_CODE)
ELOG_DECLARE_ENCODE_TYPE(int16_t, ELOG_INT16_CODE)
ELOG_DECLARE_ENCODE_TYPE(int32_t, ELOG_INT32_CODE)
ELOG_DECLARE_ENCODE_TYPE(int64_t, ELOG_INT64_CODE)
ELOG_DECLARE_ENCODE_TYPE(float, ELOG_FLOAT_CODE)
ELOG_DECLARE_ENCODE_TYPE(double, ELOG_DOUBLE_CODE)
ELOG_DECLARE_ENCODE_TYPE(bool, ELOG_BOOL_CODE)
ELOG_DECLARE_ENCODE_TYPE(char*, ELOG_STRING_CODE)
ELOG_DECLARE_ENCODE_TYPE(const char*, ELOG_STRING_CODE)
#endif

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
    inline bool isLogging(const ELogRecordBuilder* recordBuilder) const {
        return recordBuilder->getOffset() > 0;
    }

    /** @brief Queries whether the logger can issue log message with the given level. */
    inline bool canLog(ELogLevel logLevel) const {
        // for the sake of pre-init logger we allow all log messages to be logged in case log source
        // is null
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
        ELogRecordBuilder* recordBuilder = getRecordBuilder();
        if (isLogging(recordBuilder)) {
            recordBuilder = pushRecordBuilder();
        }
        startLogRecord(recordBuilder->getLogRecord(), logLevel, file, line, function,
                       ELOG_RECORD_BINARY);
        // reserve 1 byte for parameter count
        uint8_t paramCountReserve = 0;
        recordBuilder->appendData(paramCountReserve);
        // append early format string, instead of passing it around in variadic template
        // NOTE: we must append terminating null as well, for deserialization
        recordBuilder->append(fmt, strlen(fmt) + 1);
        encodeMsg(recordBuilder, 0, args...);
        finishLog(recordBuilder);
    }

    /** @brief Logs a binary log record. */
    template <typename... Args>
    void logBinaryCached(ELogLevel logLevel, const char* file, int line, const char* function,
                         ELogCacheEntryId cacheEntryId, Args... args) {
        ELogRecordBuilder* recordBuilder = getRecordBuilder();
        if (isLogging(recordBuilder)) {
            recordBuilder = pushRecordBuilder();
        }
        startLogRecord(recordBuilder->getLogRecord(), logLevel, file, line, function,
                       ELOG_RECORD_BINARY | ELOG_RECORD_FMT_CACHED);
        // reserve 1 byte for parameter count
        uint8_t paramCountReserve = 0;
        recordBuilder->appendData(paramCountReserve);
        // append early cache entry id, instead of passing it around in variadic template
        // NOTE: we must append terminating null as well, for deserialization
        recordBuilder->appendData(cacheEntryId);
        encodeMsg(recordBuilder, 0, args...);
        finishLog(recordBuilder);
    }

    /** @brief Resolves a binary log record, putting the resolved message into a log buffer. */
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

    /** @brief Push current builder on builder stack and open a new builder. */
    virtual ELogRecordBuilder* pushRecordBuilder() = 0;

    /** @brief Pop current builder from builder stack and restore previous builder. */
    virtual void popRecordBuilder() = 0;

    /** @brief Finish logging (default behavior: finalize formatting and send to log target). */
    virtual void finishLog(ELogRecordBuilder* recordBuilder);

private:
    /** @var The originating log source. */
    ELogSource* m_logSource;

    /**
     * @brief Starts a multi-part log message.
     * @param logLevel The log level. No log level checking takes place.
     */
    void startLogRecord(ELogRecord& logRecord, ELogLevel logLevel, const char* file, int line,
                        const char* function, uint8_t flags = ELOG_RECORD_FORMATTED);

    inline void appendMsgV(ELogRecordBuilder* recordBuilder, const char* fmt, va_list args) {
        va_list argsCopy;
        va_copy(argsCopy, args);
        (void)recordBuilder->appendV(fmt, args);
        va_end(argsCopy);
    }

    inline void appendMsg(ELogRecordBuilder* recordBuilder, const char* msg) {
        (void)recordBuilder->append(msg);
    }

#ifdef ELOG_ENABLE_FMT_LIB
    template <typename T, typename... Args>
    void encodeMsg(ELogRecordBuilder* recordBuilder, uint64_t paramCount, T arg, Args... args) {
        recordBuilder->appendData(encodeType<T>());
        recordBuilder->appendData(arg);
        encodeMsg(recordBuilder, paramCount + 1, args...);
    }

    template <typename... Args>
    void encodeMsg(ELogRecordBuilder* recordBuilder, uint64_t paramCount, const char* arg,
                   Args... args) {
        recordBuilder->appendData(ELOG_STRING_CODE);
        // NOTE: we must ensure terminating null is added, otherwise garbage will be added by other
        // parameters or format string
        recordBuilder->append(arg, strlen(arg) + 1);
        encodeMsg(recordBuilder, paramCount + 1, args...);
    }

    void encodeMsg(ELogRecordBuilder* recordBuilder, uint64_t paramCount) {
        recordBuilder->appendDataAt((uint8_t)paramCount, 0);
    }
#endif
};

}  // namespace elog

#endif  // __ELOG_LOGGER_H__