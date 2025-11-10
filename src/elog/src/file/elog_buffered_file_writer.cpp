#include "file/elog_buffered_file_writer.h"

#ifdef ELOG_MSVC
#include <io.h>
#else
#include <unistd.h>
#endif

#include <cassert>
#include <cinttypes>
#include <cstring>

#include "elog_report.h"

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogBufferedFileWriter)

bool ELogBufferedStats::initialize(uint32_t maxThreads) {
    if (!ELogStats::initialize(maxThreads)) {
        return false;
    }
    if (!m_bufferWriteCount.initialize(maxThreads) || !m_bufferByteCount.initialize(maxThreads) ||
        !m_bufferWriteFailCount.initialize(maxThreads) ||
        !m_bufferByteFailCount.initialize(maxThreads)) {
        ELOG_REPORT_ERROR("Failed to initialize buffered file target statistics variables");
        terminate();
        return false;
    }
    return true;
}

void ELogBufferedStats::terminate() {
    ELogStats::terminate();
    m_bufferWriteCount.terminate();
    m_bufferByteCount.terminate();
    m_bufferWriteFailCount.terminate();
    m_bufferByteFailCount.terminate();
}

void ELogBufferedStats::toString(ELogBuffer& buffer, ELogTarget* logTarget,
                                 const char* msg /* = "" */) {
    ELogStats::toString(buffer, logTarget, msg);
    uint64_t bufferWriteCount = m_bufferWriteCount.getSum();
    buffer.appendArgs("\tBuffer write count: %" PRIu64 "\n", bufferWriteCount);
    if (bufferWriteCount > 0) {
        uint64_t avgBufferBytes = m_bufferByteCount.getSum() / bufferWriteCount;
        buffer.appendArgs("\tAverage buffer size: %" PRIu64 " bytes\n", avgBufferBytes);
    } else {
        buffer.append("\tAverage buffer size: N/A\n");
    }
    uint64_t bufferWriteFailCount = m_bufferWriteFailCount.getSum();
    if (bufferWriteFailCount > 0) {
        buffer.appendArgs("\tBuffer write fail count: %" PRIu64 "\n", bufferWriteFailCount);
        uint64_t avgBufferBytes = m_bufferWriteFailCount.getSum() / bufferWriteFailCount;
        buffer.appendArgs("\tAverage failed buffer size: %" PRIu64 " bytes\n", avgBufferBytes);
    }
}

void ELogBufferedStats::resetThreadCounters(uint64_t slotId) {
    ELogStats::resetThreadCounters(slotId);
    m_bufferWriteCount.reset(slotId);
    m_bufferByteCount.reset(slotId);
}

void ELogBufferedFileWriter::setFileHandle(FILE* fileHandle) {
#ifdef ELOG_WINDOWS
    m_fd = _fileno(fileHandle);
#else
    m_fd = fileno(fileHandle);
#endif
    m_logBuffer.resize(m_bufferSizeBytes);
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

bool ELogBufferedFileWriter::flushLogBuffer() {
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
        if (!flushLogBuffer()) {
            return false;
        }
        assert(m_bufferOffset == 0);
    }

    // if there is still no room then write entire message directly to file
    if (m_bufferOffset + length > m_logBuffer.size()) {
        // cannot buffer, message is too large, do direct write instead
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

bool ELogBufferedFileWriter::writeToFile(const char* buffer, size_t length) {
    // NOTE: in case the buffer size is zero, and we have direct write to file, the documentation
    // states that write() is atomic and does not require a lock, BUT it does not guarantee that all
    // bytes are written, so in case log messages are not to be mixed with each other in a
    // multi-threaded scenario, a lock is needed

    // in case the buffer size is greater than zero, and a buffer is used, then a lock is required,
    // otherwise behavior is undefined (i.e. core dump is expected)

    // write log message fully to file
#ifdef ELOG_MSVC
    typedef uint32_t pos_type_t;
#else
    typedef size_t pos_type_t;
#endif
    pos_type_t pos = 0;
    while (pos < length) {
#ifdef ELOG_MSVC
        if (length > UINT32_MAX) {
            ELOG_REPORT_MODERATE_ERROR_DEFAULT(
                "Cannot write more than %u bytes to a file at once on Windows/MSVC (requested for "
                "%zu)",
                (unsigned)UINT32_MAX, length);
            return false;
        }
        uint32_t length32 = (uint32_t)length;
        int res = _write(m_fd, buffer + pos, length32 - pos);
#else
        ssize_t res = write(m_fd, buffer + pos, length - pos);
#endif
        if (res == -1) {
            if (m_stats != nullptr && m_enableStats) {
                m_stats->incrementBufferWriteFailCount();
                m_stats->addBufferBytesFailCount(length);
            }
            ELOG_REPORT_MODERATE_SYS_ERROR_DEFAULT(write, "Failed to write %zu bytes to log file",
                                                   length - pos);
            return false;
        }
        pos += (pos_type_t)res;
    }

    if (m_stats != nullptr && m_enableStats) {
        m_stats->incrementBufferWriteCount();
        m_stats->addBufferBytesCount(length);
    }
    return true;
}

}  // namespace elog
