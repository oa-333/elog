#ifndef __ELOG_BUFFER_H__
#define __ELOG_BUFFER_H__

#include <cassert>
#include <cstdalign>
#include <cstdarg>
#include <cstdint>
#include <cstring>

#include "elog_def.h"

namespace elog {

/**
 * @def The fixed buffer size used for logging. We use this to make sure entire struct does not
 * spill over to a cache line.
 */
#define ELOG_BUFFER_SIZE (1024 - 3 * sizeof(uint64_t))

// TODO: fix terminology here - capacity, size, resize/reserve, offset/length, etc.

/**
 * @brief A fixed size buffer that may transition to dynamic size buffer. This is required by the
 * logger, which maintains a thread local buffer. Under MinGW this causes crash during application
 * shutdown, since for some reason, TLS destructors being run during DLL unload cannot deallocate
 * buffers when calling free. It is unclear whether there is a collision with the C runtime code.
 * Using a fixed buffer (so we avoid calling free() in buffer's destructor) is a bit limiting. So a
 * fixed buffer is used, but if more space is needed by some long log message, it transitions to a
 * dynamic buffer, and the logger is required to release it as soon as it finished logging.
 */
class ELOG_API ELogBuffer {
public:
    /** @brief Constructor. */
    ELogBuffer()
        : m_dynamicBuffer(nullptr), m_bufferSize(ELOG_BUFFER_SIZE), m_offset(0), m_bufferFull(0) {}

    ELogBuffer(const ELogBuffer& buffer)
        : m_dynamicBuffer(nullptr), m_bufferSize(ELOG_BUFFER_SIZE), m_offset(0), m_bufferFull(0) {
        assign(buffer.getRef(), buffer.getOffset());
    }

    /** @brief Destructor. */
    ~ELogBuffer();

    /** @brief Returns a reference to the internal buffer. */
    inline char* getRef() { return m_dynamicBuffer ? m_dynamicBuffer : (char*)m_fixedBuffer; }

    /** @brief Returns a reference to the internal buffer. */
    inline const char* getRef() const {
        return m_dynamicBuffer ? m_dynamicBuffer : (const char*)m_fixedBuffer;
    }

    /** @brief Retrieves the current capacity of the buffer. */
    inline uint32_t size() const { return m_bufferSize; }

    /** @brief Retrieves the current offset of data stored in the buffer. */
    inline uint32_t getOffset() const { return m_offset; }

    /**
     * @brief Increases the current capacity of the buffer. If the buffer's size is already
     * greater than the required size then no action takes place.
     * @param newSize The required new size.
     * @return true If operation succeeded, otherwise false.
     */
    bool resize(uint32_t newSize);

    /** @brief Resets the buffer to original state. Releases the dynamic buffer if needed. */
    void reset();

    /** @brief Finalizes the log buffer. */
    inline void finalize() {
        if (m_bufferFull) {
            // put terminating null in case buffer got full
            getRef()[size() - 1] = 0;
        }
    }

    /** @brief Assigns a string value to the buffer. Discards previous contents. */
    inline bool assign(const char* msg, size_t len = 0) {
        reset();
        return append(msg, len);
    }

    /** @brief Assigns a log buffer to another buffer. Discards previous contents. */
    inline bool assign(const ELogBuffer& logBuffer) {
        return assign(logBuffer.getRef(), logBuffer.getOffset());
    }

    /** @brief Appends a formatted message to the log buffer. */
    bool appendV(const char* fmt, va_list ap);

    /** @brief Appends a string to the log buffer. */
    bool append(const char* msg, size_t len = 0);

    /** @brief Appends a char repeatedly to the log buffer. */
    inline bool append(size_t count, char c) {
        if (m_bufferFull) {
            return false;
        }
        if (!ensureBufferLength(count)) {
            return false;
        }
        memset(getRef() + m_offset, c, count);
        m_offset += count;
        return true;
    }

    /** @brief Ensures the log buffer has enough bytes. */
    inline bool ensureBufferLength(uint32_t requiredBytes) {
        bool res = true;
        if (size() - m_offset < requiredBytes) {
            res = resize(m_offset + requiredBytes);
            if (!res) {
                m_bufferFull = true;
            }
        }
        return res;
    }

    inline ELogBuffer& operator=(const ELogBuffer& buffer) {
        assign(buffer.getRef(), buffer.getOffset());
        return *this;
    }

private:
    char m_fixedBuffer[ELOG_BUFFER_SIZE];
    char* m_dynamicBuffer;
    uint32_t m_bufferSize;
    uint32_t m_offset;
    uint64_t m_bufferFull;
};

}  // namespace elog

#endif  // __ELOG_BUFFER_H__