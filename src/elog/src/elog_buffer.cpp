#include "elog_buffer.h"

#include <cstdio>
#include <cstdlib>

#include "elog_common.h"
#include "elog_error.h"

namespace elog {

ELogBuffer::~ELogBuffer() {
#ifndef __MINGW32__
    reset();
#endif
}

bool ELogBuffer::resize(uint32_t newSize) {
    if (m_bufferSize < newSize) {
        bool shouldCopy = (m_dynamicBuffer == nullptr);
        char* newBuffer = (char*)realloc(m_dynamicBuffer, newSize);
        if (newBuffer == nullptr) {
            return false;
        }
        m_dynamicBuffer = newBuffer;
        if (shouldCopy) {
            elog_strncpy(m_dynamicBuffer, m_fixedBuffer, m_bufferSize);
        }
        m_bufferSize = newSize;
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

bool ELogBuffer::appendV(const char* fmt, va_list ap) {
    if (m_bufferFull) {
        return false;
    }
    size_t sizeLeft = size() - m_offset;
    int res = vsnprintf(getRef() + m_offset, sizeLeft, fmt, ap);
    // return value does not include the terminating null, and number of copied characters,
    // including the terminating null, will not exceed size, so if res==size it means size - 1
    // characters were copied and one more terminating null, meaning one character was lost.
    // if res > size if definitely means buffer was too small, and res shows the required size
    if (res < sizeLeft) {
        m_offset += res;
        return true;
    }

    // buffer too small
    if (!ensureBufferLength(res)) {
        return false;
    }
    // this time we must succeed
    sizeLeft = size() - m_offset;
    res = vsnprintf(getRef() + m_offset, sizeLeft, fmt, ap);
    if (res < sizeLeft) {
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
    if (!ensureBufferLength(len)) {
        return false;
    }
    m_offset += elog_strncpy(getRef() + m_offset, msg, size() - m_offset, len);
    return true;
}

}  // namespace elog
