#include "elog_file_target.h"

#include "elog_system.h"

namespace elog {

ELogFileTarget::ELogFileTarget(const char* filePath, ELogFlushPolicy* flushPolicy)
    : ELogAbstractTarget(flushPolicy),
      m_filePath(filePath),
      m_fileHandle(nullptr),
      m_shouldClose(false) {
    m_fileHandle = fopen(filePath, "a");
    if (m_fileHandle == nullptr) {
        ELOG_SYS_ERROR(ELogSystem::getDefaultLogger(), fopen,
                       "Failed to redirect log message to file %s", filePath);
    } else {
        m_shouldClose = true;
    }
}

ELogFileTarget::~ELogFileTarget() {
    if (m_fileHandle != nullptr && m_shouldClose) {
        fclose(m_fileHandle);
    }
}

bool ELogFileTarget::start() {
    m_fileHandle = fopen(m_filePath.c_str(), "a");
    if (m_fileHandle == nullptr) {
        ELOG_SYS_ERROR(ELogSystem::getDefaultLogger(), fopen, "Failed to open log file %s",
                       m_filePath.c_str());
        return false;
    }
    return true;
}

bool ELogFileTarget::stop() {
    if (m_fileHandle != nullptr && m_shouldClose) {
        if (fclose(m_fileHandle) == -1) {
            ELOG_SYS_ERROR(ELogSystem::getDefaultLogger(), fopen, "Failed to close log file %s",
                           m_filePath.c_str());
            return false;
        }
    }
    return true;
}

void ELogFileTarget::log(const std::string& formattedLogMsg) {
    fputs(formattedLogMsg.c_str(), m_fileHandle);
}

void ELogFileTarget::flush() { fflush(m_fileHandle); }

}  // namespace elog
