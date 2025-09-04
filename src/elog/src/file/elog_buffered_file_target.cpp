#include "file/elog_buffered_file_target.h"

#include "elog_common.h"
#include "elog_report.h"

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogBufferedFileTarget)

ELogBufferedFileTarget::ELogBufferedFileTarget(const char* filePath,
                                               uint64_t bufferSizeBytes /* = 0 */,
                                               bool useLock /* = true */,
                                               ELogFlushPolicy* flushPolicy /* = nullptr */,
                                               bool enableStats /* = true */)
    : ELogTarget("file", flushPolicy, enableStats),
      m_filePath(filePath),
      m_fileWriter(bufferSizeBytes, useLock),
      m_fileHandle(nullptr),
      m_shouldClose(false) {
    if (useLock) {
        setNativelyThreadSafe();
    }
    setAddNewLine(true);
}

bool ELogBufferedFileTarget::startLogTarget() {
    if (m_fileHandle == nullptr) {
        m_fileHandle = elog_fopen(m_filePath.c_str(), "a");
        if (m_fileHandle == nullptr) {
            ELOG_REPORT_ERROR("Failed to open log file %s", m_filePath.c_str());
            return false;
        }
        m_shouldClose = true;
    }
    m_fileWriter.setFileHandle(m_fileHandle);
    // NOTE this is ok even if stats are disabled
    m_fileWriter.setStats((ELogBufferedStats*)m_stats);
    return true;
}

bool ELogBufferedFileTarget::stopLogTarget() {
    if (m_fileHandle != nullptr && m_shouldClose) {
        if (!m_fileWriter.flushLogBuffer()) {
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

void ELogBufferedFileTarget::logFormattedMsg(const char* formattedLogMsg, size_t length) {
    if (!m_fileWriter.logMsg(formattedLogMsg, length)) {
        ELOG_REPORT_TRACE("Failed to write formatted log message to buffered file writer");
    }
}

bool ELogBufferedFileTarget::flushLogTarget() {
    uint64_t slotId = m_enableStats ? m_stats->getSlotId() : ELOG_INVALID_STAT_SLOT_ID;
    if (slotId != ELOG_INVALID_STAT_SLOT_ID) {
        m_stats->incrementFlushSubmitted(slotId);
    }
    if (fflush(m_fileHandle) == EOF) {
        // TODO: should log once
        ELOG_REPORT_SYS_ERROR(fflush, "Failed to flush buffered file");
        if (slotId != ELOG_INVALID_STAT_SLOT_ID) {
            m_stats->incrementFlushFailed(slotId);
        }
        return false;
    }
    if (slotId != ELOG_INVALID_STAT_SLOT_ID) {
        m_stats->incrementFlushExecuted(slotId);
    }
    return true;
}

ELogStats* ELogBufferedFileTarget::createStats() { return new (std::nothrow) ELogBufferedStats(); }

}  // namespace elog
