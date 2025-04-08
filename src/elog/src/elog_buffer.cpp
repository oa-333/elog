#include "elog_buffer.h"

#include <cstdint>
#include <cstdlib>

namespace elog {

ELogBuffer::~ELogBuffer() {
#ifndef __MINGW32__
    reset();
#endif
}

bool ELogBuffer::resize(uint32_t newSize) {
    if (m_bufferSize < newSize) {
        char* newBuffer = (char*)realloc(m_dynamicBuffer, newSize);
        if (newBuffer == nullptr) {
            return false;
        }
        m_dynamicBuffer = newBuffer;
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
}

}  // namespace elog
