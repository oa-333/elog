#include "elog_buffer.h"

#include <cstdio>
#include <cstdlib>

#include "elog_common.h"
#include "elog_error.h"

namespace elog {

ELogBuffer::~ELogBuffer() {
    // TODO: what is this? is this because of static thread_local issues, if so that was already
    // solved, so we can remove ifdef (check this)
#ifndef __MINGW32__
    reset();
#endif
}

bool ELogBuffer::resize(uint32_t newSize) {
    if (newSize > ELOG_MAX_BUFFER_SIZE) {
        ELOG_REPORT_ERROR("Cannot resize log buffer to size %u, exceeding maximum allowed %u",
                          newSize, (unsigned)ELOG_MAX_BUFFER_SIZE);
        return false;
    }
    if (m_bufferSize < newSize) {
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
        if (shouldCopy) {
            elog_strncpy(m_dynamicBuffer, m_fixedBuffer, m_bufferSize);
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
    // return value does not include the terminating null, and number of copied characters,
    // including the terminating null, will not exceed size, so if res==size it means size - 1
    // characters were copied and one more terminating null, meaning one character was lost.
    // if res > size if definitely means buffer was too small, and res shows the required size
    if (res < sizeLeft) {
        va_end(argsCopy);
        m_offset += res;
        return true;
    }

    // buffer too small
    if (!ensureBufferLength(res)) {
        return false;
    }
    // this time we must succeed
    sizeLeft = size() - m_offset;
    res = vsnprintf(getRef() + m_offset, sizeLeft, fmt, argsCopy);
    va_end(argsCopy);
    if (res >= sizeLeft) {
        ELOG_REPORT_ERROR("Failed to format string second time");
        return false;
    }
    m_offset += res;
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
    if (!ensureBufferLength((uint32_t)len)) {
        return false;
    }
    m_offset += elog_strncpy(getRef() + m_offset, msg, size() - m_offset, len);
    return true;
}

}  // namespace elog
