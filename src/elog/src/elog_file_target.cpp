#include "elog_file_target.h"

#include "elog_system.h"

namespace elog {

ELogFileTarget::ELogFileTarget(const char* filePath, ELogFlushPolicy* flushPolicy /* = nullptr */)
    : ELogTarget("file", flushPolicy),
      m_filePath(filePath),
      m_fileHandle(nullptr),
      m_shouldClose(false) {
    setAddNewLine(true);
}

bool ELogFileTarget::startLogTarget() {
    if (m_fileHandle == nullptr) {
        m_fileHandle = fopen(m_filePath.c_str(), "a");
        if (m_fileHandle == nullptr) {
            ELogSystem::reportSysError("fopen", "Failed to open log file %s", m_filePath.c_str());
            return false;
        }
        m_shouldClose = true;
    }
    return true;
}

bool ELogFileTarget::stopLogTarget() {
    if (m_fileHandle != nullptr && m_shouldClose) {
        flush();
        if (fclose(m_fileHandle) == -1) {
            ELogSystem::reportSysError("fclose", "Failed to close log file %s", m_filePath.c_str());
            return false;
        }
    }
    return true;
}

void ELogFileTarget::logFormattedMsg(const std::string& formattedLogMsg) {
    // NOTE: according to https://gcc.gnu.org/onlinedocs/libstdc++/manual/using_concurrency.html
    // gcc documentations states that: "POSIX standard requires that C stdio FILE* operations are
    // atomic. POSIX-conforming C libraries (e.g, on Solaris and GNU/Linux) have an internal mutex
    // to serialize operations on FILE*s."
    // therefore no lock is required here
    fputs(formattedLogMsg.c_str(), m_fileHandle);
    addBytesWritten(formattedLogMsg.length());
}

void ELogFileTarget::flush() {
    if (fflush(m_fileHandle) == EOF) {
        int errCode = errno;
        ELogSystem::reportError("Failed to flush file: error code %d", errCode);
    }
}

}  // namespace elog
