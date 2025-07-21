#ifndef __ELOG_RECORD_BUILDER_H__
#define __ELOG_RECORD_BUILDER_H__

#include <cstdio>

#include "elog_buffer.h"
#include "elog_record.h"

namespace elog {

/** @brief Helper class for the ELogLogger. */
class ELOG_API ELogRecordBuilder {
public:
    ELogRecordBuilder(ELogRecordBuilder* next = nullptr) : m_next(next) {}
    ELogRecordBuilder(const ELogRecordBuilder&) = delete;
    ELogRecordBuilder(ELogRecordBuilder&&) = delete;
    ELogRecordBuilder& operator=(const ELogRecordBuilder&) = delete;
    ~ELogRecordBuilder() {}

    inline const ELogRecord& getLogRecord() const { return m_logRecord; }
    inline ELogRecord& getLogRecord() { return m_logRecord; }
    inline uint32_t getOffset() const { return m_buffer.getOffset(); }
    inline ELogRecordBuilder* getNext() { return m_next; }

    /** @brief Finalizes the log record. */
    inline void finalize() {
        m_buffer.finalize();
        m_logRecord.m_logMsg = m_buffer.getRef();
        m_logRecord.m_logMsgLen = m_buffer.getOffset();
    }

    /** @brief Resets the log record. */
    inline void reset() { m_buffer.reset(); }

    /** @brief Appends a formatted message to the log buffer. */
    inline bool appendV(const char* fmt, va_list args) { return m_buffer.appendV(fmt, args); }

    /** @brief Appends a string to the log buffer. */
    inline bool append(const char* msg, size_t len = 0) { return m_buffer.append(msg, len); }

    /** @brief Appends data (binary form). */
    template <typename T>
    inline bool appendData(const T& value) {
        return m_buffer.appendData(value);
    }

    /** @brief Appends data (binary form). */
    template <typename T>
    inline bool appendDataAt(const T& value, uint32_t offset) {
        return m_buffer.writeRawAt((const char*)&value, sizeof(T), offset);
    }

private:
    ELogBuffer m_buffer;
    ELogRecord m_logRecord;
    ELogRecordBuilder* m_next;
};

}  // namespace elog

#endif  // __ELOG_RECORD_BUILDER_H__