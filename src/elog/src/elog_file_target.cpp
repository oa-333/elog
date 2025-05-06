#include "elog_file_target.h"

#include "elog_system.h"

namespace elog {

ELogFileTarget::ELogFileTarget(const char* filePath, ELogFlushPolicy* flushPolicy /* = nullptr */)
    : ELogTarget(flushPolicy), m_filePath(filePath), m_fileHandle(nullptr), m_shouldClose(false) {
    setAddNewLine(true);
}

ELogFileTarget::~ELogFileTarget() {}

bool ELogFileTarget::start() {
    if (m_fileHandle == nullptr) {
        m_fileHandle = fopen(m_filePath.c_str(), "a");
        if (m_fileHandle == nullptr) {
            ELOG_SYS_ERROR(fopen, "Failed to open log file %s", m_filePath.c_str());
            return false;
        }
        m_shouldClose = true;
    }
    return true;
}

bool ELogFileTarget::stop() {
    if (m_fileHandle != nullptr && m_shouldClose) {
        if (fclose(m_fileHandle) == -1) {
            ELOG_SYS_ERROR(fopen, "Failed to close log file %s", m_filePath.c_str());
            return false;
        }
    }
    return true;
}

void ELogFileTarget::logFormattedMsg(const std::string& formattedLogMsg) {
    fputs(formattedLogMsg.c_str(), m_fileHandle);
}

void ELogFileTarget::flush() { fflush(m_fileHandle); }

}  // namespace elog
