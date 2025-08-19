#include "elog_buffer.h"

#include <cstdio>
#include <cstdlib>

#include "elog_common.h"
#include "elog_report.h"

namespace elog {

ELogBuffer::~ELogBuffer() {
    // TODO: what is this? is this because of static thread_local issues, if so that was already
    // solved, so we can remove ifdef (check this)
#ifndef __MINGW32__
    reset();
#endif
}

bool ELogBuffer::resize(uint32_t newSize) {
    if (m_bufferSize < newSize) {
        if (newSize > ELOG_MAX_BUFFER_SIZE) {
            ELOG_REPORT_ERROR("Cannot resize log buffer to size %zu, exceeding maximum allowed %zu",
                              newSize, ELOG_MAX_BUFFER_SIZE);
            return false;
        }
        // allocate a bit more so we avoid another realloc and copy if possible
        uint32_t actualNewSize = m_bufferSize;
        while (actualNewSize < newSize) {
            actualNewSize *= 2;
        }
        bool shouldCopy = (m_dynamicBuffer == nullptr);
        char* newBuffer = (char*)realloc(m_dynamicBuffer, actualNewSize);
        if (newBuffer == nullptr) {
            return false;
        }
        m_dynamicBuffer = newBuffer;
        if (shouldCopy && m_offset > 0) {
            memcpy(m_dynamicBuffer, m_fixedBuffer, m_offset);
        }
        m_bufferSize = actualNewSize;
    }
    return true;
}

void ELogBuffer::reset() {
    if (m_dynamicBuffer != nullptr) {
        free(m_dynamicBuffer);
        m_dynamicBuffer = nullptr;
    }
    m_bufferSize = ELOG_BUFFER_SIZE;
    m_offset = 0;
    m_bufferFull = false;
}

bool ELogBuffer::appendV(const char* fmt, va_list args) {
    if (m_bufferFull) {
        return false;
    }

    // NOTE: if buffer is too small then args is corrupt and cannot be reused, so we have no option
    // but prepare a copy in advance, even though mostly it will not be used
    va_list argsCopy;
    va_copy(argsCopy, args);
    uint32_t sizeLeft = size() - m_offset;
    int res = vsnprintf(getRef() + m_offset, sizeLeft, fmt, args);
    if (res < 0) {
        // output error occurred, report this, this is highly unexpected
        ELOG_REPORT_ERROR("Failed to format message buffer, I/O error");
        return false;
    }

    // now we can safely make the cast
    uint32_t res32 = (uint32_t)res;

    // NOTE: return value does not include the terminating null, and number of copied characters,
    // including the terminating null, will not exceed size, so if res==size it means size - 1
    // characters were copied and one more terminating null, meaning one character was lost.
    // if res > size if definitely means buffer was too small, and res shows the required size

    // NOTE: cast to int is safe (since size is limited to ELOG_MAX_BUFFER_SIZE = 16KB)
    if (res32 < sizeLeft) {
        va_end(argsCopy);
        m_offset += res;
        return true;
    }

    // buffer too small (make room also for terminating null, otherwise we can fail again)
    if (!ensureBufferLength(res32 + 1)) {
        return false;
    }
    // this time we must succeed
    sizeLeft = size() - m_offset;
    res = vsnprintf(getRef() + m_offset, sizeLeft, fmt, argsCopy);
    va_end(argsCopy);

    // check again for error
    if (res < 0) {
        // output error occurred, report this, this is highly unexpected
        ELOG_REPORT_ERROR("Failed to format message buffer (I/O error, second time)");
        return false;
    }

    // now we can safely make the cast
    res32 = (uint32_t)res;

    // NOTE: cast to int is safe (since size is limited to ELOG_MAX_BUFFER_SIZE = 16KB)
    if (res32 >= sizeLeft) {
        ELOG_REPORT_ERROR(
            "Internal error, failed to format string second time (unexpected truncation)");
        return false;
    }

    // NOTE: we continue appending at the same position of the terminating null
    m_offset += res32;
    return true;
}

bool ELogBuffer::append(const char* msg, size_t len /* = 0 */) {
    if (m_bufferFull) {
        return false;
    }
    if (len == 0) {
        len = strlen(msg);
    }
    // NOTE: be careful of size truncation
    if (len >= UINT32_MAX) {
        return false;
    }
    // make room also for terminating null
    if (!ensureBufferLength((uint32_t)(len + 1))) {
        return false;
    }
    // NOTE: at this point it is guaranteed that the added len will not exceed maximum buffer size,
    // otherwise ensureBufferLength() would have failed, so we can also copy terminating null
    memcpy(getRef() + m_offset, msg, len + 1);

    // NOTE: offset points to the terminating null
    m_offset += (uint32_t)len;

    // NOTE: we continue appending at the same position of the terminating null
    return true;
}

bool ELogBuffer::appendRaw(const char* data, size_t len) {
    if (m_bufferFull) {
        return false;
    }
    // NOTE: be careful of size truncation
    if (len >= UINT32_MAX) {
        return false;
    }
    if (!ensureBufferLength((uint32_t)len)) {
        return false;
    }
    // NOTE: at this point it is guaranteed that the added len will not exceed maximum buffer size,
    // otherwise ensureBufferLength() would have failed
    memcpy(getRef() + m_offset, data, len);
    m_offset += (uint32_t)len;
    return true;
}

bool ELogBuffer::writeRawAt(const char* data, size_t len, size_t offset) {
    // NOTE: be careful of size truncation
    if (len >= UINT32_MAX) {
        return false;
    }

    size_t targetSize = offset + len;
    if (targetSize > m_bufferSize) {
        if (m_bufferFull) {
            return false;
        }
        size_t requiredLen = (targetSize - m_offset);
        if (!ensureBufferLength((uint32_t)requiredLen)) {
            return false;
        }
    }

    // NOTE: at this point it is guaranteed that the added len will not exceed maximum buffer size,
    // otherwise ensureBufferLength() would have failed
    memcpy(getRef() + offset, data, len);
    if (targetSize > m_offset) {
        m_offset = (uint32_t)targetSize;
    }
    return true;
}

}  // namespace elog
