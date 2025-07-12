#include "elog_pre_init_logger.h"

#include "elog_aligned_alloc.h"
#include "elog_error.h"
#include "elog_system.h"
#include "elog_target.h"

namespace elog {

void ELogPreInitLogger::writeAccumulatedLogMessages(ELogTarget* logTarget) {
    // use default logger
    ELogLogger* logger = ELogSystem::getDefaultLogger();

    // first check affinity
    ELogTargetAffinityMask mask = logger->getLogSource()->getLogTargetAffinityMask();
    if (!ELOG_HAS_TARGET_AFFINITY_MASK(mask, logTarget->getId())) {
        // not logging due to affinity
        return;
    }

    // now check pass key
    ELogPassKey passKey = logTarget->getPassKey();
    if (passKey != ELOG_NO_PASSKEY && !logger->getLogSource()->hasPassKey(passKey)) {
        return;
    }

    // finally move on to actual logging
    for (ELogRecordBuilder* recordBuilder : m_accumulatedRecordBuilders) {
        ELogRecord& logRecord = recordBuilder->getLogRecord();
        logRecord.m_logger = logger;
        if (ELogSystem::filterLogMsg(logRecord)) {
            logTarget->log(logRecord);
        }
    }
}

void ELogPreInitLogger::discardAccumulatedLogMessages() {
    for (ELogRecordBuilder* recordBuilder : m_accumulatedRecordBuilders) {
        recordBuilder->reset();
        elogAlignedFreeObject(recordBuilder);
    }
    m_accumulatedRecordBuilders.clear();
}

ELogRecordBuilder* ELogPreInitLogger::getRecordBuilder() {
    if (m_recordBuilder == nullptr) {
        m_recordBuilder = elogAlignedAllocObject<ELogRecordBuilder>(ELOG_CACHE_LINE, nullptr);
    }
    return m_recordBuilder;
}

const ELogRecordBuilder* ELogPreInitLogger::getRecordBuilder() const {
    if (m_recordBuilder == nullptr) {
        const_cast<ELogRecordBuilder*&>(m_recordBuilder) =
            elogAlignedAllocObject<ELogRecordBuilder>(ELOG_CACHE_LINE, nullptr);
    }
    return m_recordBuilder;
}

ELogRecordBuilder* ELogPreInitLogger::pushRecordBuilder() {
    ELogRecordBuilder* recordBuilder =
        elogAlignedAllocObject<ELogRecordBuilder>(ELOG_CACHE_LINE, m_recordBuilder);
    if (recordBuilder != nullptr) {
        m_recordBuilder = recordBuilder;
    }
    return m_recordBuilder;
}

void ELogPreInitLogger::popRecordBuilder() {
    if (m_recordBuilder != nullptr) {
        ELogRecordBuilder* next = m_recordBuilder->getNext();
        // do not free record builder, this will take place only explicitly
        // elogAlignedFreeObject(m_recordBuilder);
        m_recordBuilder = next;
    }
}

void ELogPreInitLogger::finishLog(ELogRecordBuilder* recordBuilder) {
    if (isLogging(recordBuilder)) {
        // NOTE: new line character at the end of the line is added by each log target individually
        // add terminating null and transfer to log record
        recordBuilder->finalize();

        // instead of sending to log targets, we accumulate into a list
        m_accumulatedRecordBuilders.push_back(recordBuilder);

        // do not reset record builder, but rather pop it without deleting it
        popRecordBuilder();
    } else {
        ELOG_REPORT_ERROR("attempt to end log message without start-log being issued first\n");
    }
}

}  // namespace elog