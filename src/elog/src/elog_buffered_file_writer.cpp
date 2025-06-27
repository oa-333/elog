#include "elog_buffered_file_writer.h"

#ifdef ELOG_MSVC
#include <io.h>
#else
#include <unistd.h>
#endif

#include <cassert>
#include <cstring>

#include "elog_error.h"

namespace elog {

void ELogBufferedFileWriter::setFileHandle(FILE* fileHandle) {
    m_fd = fileno(fileHandle);
    m_logBuffer.resize(m_bufferSize);
    m_bufferOffset = 0;
}

bool ELogBufferedFileWriter::logMsg(const char* formattedLogMsg, size_t length) {
    if (m_useLock) {
        std::unique_lock<std::mutex> lock(m_lock);
        return logMsgUnlocked(formattedLogMsg, length);
    } else {
        return logMsgUnlocked(formattedLogMsg, length);
    }
}

bool ELogBufferedFileWriter::finishLog() {
    // no locking is required here
    if (m_bufferOffset > 0) {
        if (!writeToFile(&m_logBuffer[0], m_bufferOffset)) {
            return false;
        }
        m_bufferOffset = 0;
    }
    return true;
}

bool ELogBufferedFileWriter::logMsgUnlocked(const char* formattedLogMsg, size_t length) {
    // write buffer to file if there is not enough room (this way only whole messages are written to
    // log file)
    if (m_bufferOffset + length > m_logBuffer.size()) {
        if (!writeToFile(&m_logBuffer[0], m_bufferOffset)) {
            return false;
        }
        m_bufferOffset = 0;
    }

    // if there is still no room them write entire message directly to file
    if (m_bufferOffset + length > m_logBuffer.size()) {
        // cannot buffer, message is too large, do direct write instead
        assert(m_bufferOffset == 0);
        if (!writeToFile(formattedLogMsg, length)) {
            return false;
        }
    } else {
        // otherwise append message to buffer (do not append terminating null)
        memcpy(&m_logBuffer[m_bufferOffset], formattedLogMsg, length);
        m_bufferOffset += length;
    }
    return true;
}

bool ELogBufferedFileWriter::writeToFile(const char* buffer, uint32_t length) {
    // NOTE: in case the buffer size is zero, and we have direct write to file, the documentation
    // states that write() is atomic and doe snot require a lock, BUT it does not guarantee that all
    // bytes a re written, so in case log messages are not to be mixed with each other in a
    // multi-threaded scenario, a lock is needed

    // in case the buffer size is greater than zero, and a buffer is used, then a lock is required,
    // otherwise behavior is undefined (i.e. core dump is expected)

    // write log message fully to file
    uint32_t pos = 0;
    while (pos < length) {
#ifdef ELOG_MSVC
        int res = _write(m_fd, buffer + pos, length - pos);
#else
        ssize_t res = write(m_fd, buffer + pos, length - pos);
#endif
        if (res == -1) {
            int errCode = errno;
            ELOG_REPORT_ERROR("Failed to write %u bytes to log file: system error %d", length,
                              errCode);
            return false;
        }
        pos += res;
    }
    return true;
}

}  // namespace elog
