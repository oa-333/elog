#ifndef __ELOG_RECORD_BUILDER_H__
#define __ELOG_RECORD_BUILDER_H__

#include <cstdio>

#include "elog_buffer.h"
#include "elog_record.h"

namespace elog {

/** @brief Helper class for the ELogLogger. */
class ELOG_API ELogRecordBuilder {
public:
    ELogRecordBuilder(ELogRecordBuilder* next = nullptr)
        : m_offset(0), m_bufferFull(false), m_next(next) {}
    ~ELogRecordBuilder() {}

    inline const ELogRecord& getLogRecord() const { return m_logRecord; }
    inline ELogRecord& getLogRecord() { return m_logRecord; }
    inline uint32_t getOffset() const { return m_offset; }
    inline ELogRecordBuilder* getNext() { return m_next; }

    /** @brief Finalizes the log record. */
    inline void finalize() {
        if (m_bufferFull) {
            // put terminating null in case buffer got full
            m_buffer.getRef()[m_buffer.size() - 1] = 0;
        }
        m_logRecord.m_logMsg = m_buffer.getRef();
    }

    /** @brief Resets the log record. */
    inline void reset() {
        m_buffer.reset();
        m_offset = 0;
        m_bufferFull = false;
    }

    /** @brief Appends a formatted message to the log buffer. */
    inline void appendV(const char* fmt, va_list ap) {
        m_offset += vsnprintf(m_buffer.getRef() + m_offset, m_buffer.size() - m_offset, fmt, ap);
    }

    /** @brief Appends a string to the log buffer. */
    inline void append(const char* msg) {
        m_offset += elog_strncpy(m_buffer.getRef() + m_offset, msg, m_buffer.size() - m_offset);
    }

    /** @brief Ensures the log buffer has enough bytes. */
    inline bool ensureBufferLength(uint32_t requiredBytes) {
        bool res = true;
        if (m_buffer.size() - m_offset < requiredBytes) {
            res = m_buffer.resize(m_offset + requiredBytes);
            if (!res) {
                m_bufferFull = true;
            }
        }
        return res;
    }

private:
    ELogBuffer m_buffer;
    uint32_t m_offset;
    bool m_bufferFull;
    ELogRecord m_logRecord;
    ELogRecordBuilder* m_next;

    uint32_t elog_strncpy(char* dest, const char* src, uint32_t dest_len);
};

}  // namespace elog

#endif  // __ELOG_RECORD_BUILDER_H__