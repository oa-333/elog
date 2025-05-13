#include "elog_buffered_file_target.h"

#include "elog_error.h"

namespace elog {

ELogBufferedFileTarget::ELogBufferedFileTarget(const char* filePath, uint32_t bufferSize /* = 0 */,
                                               bool useLock /* = true */,
                                               ELogFlushPolicy* flushPolicy /* = nullptr */)
    : ELogTarget("file", flushPolicy),
      m_fileWriter(bufferSize, useLock),
      m_filePath(filePath),
      m_fileHandle(nullptr),
      m_shouldClose(false) {
    setAddNewLine(true);
}

bool ELogBufferedFileTarget::startLogTarget() {
    if (m_fileHandle == nullptr) {
        m_fileHandle = fopen(m_filePath.c_str(), "a");
        if (m_fileHandle == nullptr) {
            ELOG_REPORT_SYS_ERROR(fopen, "Failed to open log file %s", m_filePath.c_str());
            return false;
        }
        m_shouldClose = true;
    }
    m_fileWriter.setFileHandle(m_fileHandle);
    return true;
}

bool ELogBufferedFileTarget::stopLogTarget() {
    if (m_fileHandle != nullptr && m_shouldClose) {
        if (!m_fileWriter.finishLog()) {
            ELOG_REPORT_ERROR("Failed to write last buffer data into log file");
            return false;
        }
        flush();
        if (fclose(m_fileHandle) == -1) {
            ELOG_REPORT_SYS_ERROR(fopen, "Failed to close log file %s", m_filePath.c_str());
            return false;
        }
    }
    return true;
}

void ELogBufferedFileTarget::logFormattedMsg(const std::string& formattedLogMsg) {
    if (m_fileWriter.logMsg(formattedLogMsg)) {
        addBytesWritten(formattedLogMsg.length());
    }
}

void ELogBufferedFileTarget::flush() {
    if (fflush(m_fileHandle) == EOF) {
        int errCode = errno;
        ELOG_REPORT_ERROR("Failed to flush file: error code %d", errCode);
    }
}

}  // namespace elog
