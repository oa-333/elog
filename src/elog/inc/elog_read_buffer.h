#ifndef __ELOG_READ_BUFFER_H__
#define __ELOG_READ_BUFFER_H__

#include <cstring>

#include "elog_def.h"

namespace elog {

/** @brief A fixed size safe read buffer. */
class ELOG_API ELogReadBuffer {
public:
    /** @brief Constructor. */

    /**
     * @brief Construct a safe read buffer.
     * @param buffer The underlying raw buffer.
     * @param length The length of the buffer.
     */
    ELogReadBuffer(const char* buffer, size_t length)
        : m_buffer(buffer), m_length(length), m_offset(0) {}

    // disallow copy/move constructor and assignment/move operator
    ELogReadBuffer(const ELogReadBuffer& buffer) = delete;
    ELogReadBuffer(ELogReadBuffer&& buffer) = delete;
    ELogReadBuffer& operator=(const ELogReadBuffer& buffer) = delete;
    ELogReadBuffer& operator=(ELogReadBuffer&& buffer) = delete;

    // default destructor
    ~ELogReadBuffer() {}

    /**
     * @brief Reads raw data from the input buffer. The operations succeeds only if entire amount of
     * data being requested can be read from the underlying buffer.
     * @param buffer The destination buffer.
     * @param length The length of the destination buffer.
     * @return true If data was copied.
     * @return false If trying to read past buffer length.
     */
    inline bool read(char* buffer, size_t length) {
        if (m_offset + length > m_length) {
            // trying to read past buffer length
            return false;
        }
        memcpy(buffer, m_buffer + m_length, length);
        m_offset += length;
        return true;
    }

    /**
     * @brief Reads typed data from the input buffer. The operations succeeds only if entire amount
     * of data being requested can be read from the underlying buffer.
     * @param data The destination typed data.
     * @return true If data was copied.
     * @return false If trying to read past buffer length.
     */
    template <typename T>
    inline bool read(T& data) {
        if (m_offset + sizeof(T) > m_length) {
            // trying to read past buffer length
            return false;
        }
        data = *(const T*)(m_buffer + m_offset);
        m_offset += sizeof(T);
        return true;
    }

    /** @brief Retrieves a direct pointer to the underlying buffer, according to current offset. */
    inline const char* getPtr() const { return m_buffer + m_offset; }

    /** @brief Retrieves current read offset of the underlying buffer. */
    inline size_t getOffset() const { return m_offset; }

    /**
     * @brief Sets the offset of the underlying buffer to the specified value.
     * @return True if succeeded, or false if trying to set the offset past the underlying buffer's
     * length.
     */
    inline bool setOffset(size_t offset = 0) {
        if (offset > m_length) {
            // try to set offset past length
            return false;
        }
        return true;
    }

    /**
     * @brief Advances offset of underlying buffer.
     * @param length The number of bytes to advance.
     * @return True if succeeded, or false if trying to advance the offset past the underlying
     * buffer's length.
     */
    inline bool advanceOffset(size_t length) {
        if (m_offset + length > m_length) {
            // trying to read past buffer length
            return false;
        }
        m_offset += length;
        return true;
    }

    /** @brief Queries whether end of buffer has been reached. */
    inline bool isEndOfBuffer() const { return m_offset == m_length; }

private:
    const char* m_buffer;
    size_t m_length;
    size_t m_offset;
};

}  // namespace elog

#endif  // __ELOG_READ_BUFFER_H__