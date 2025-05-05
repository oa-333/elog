#include "elog_quantum_target.h"

#include <cassert>

#include "elog_system.h"

namespace elog {

ELogQuantumTarget::ELogQuantumTarget(ELogTarget* logTarget, uint32_t bufferSize)
    : m_writePos(0), m_readPos(0), m_logTarget(logTarget), m_stop(false) {
    m_ringBuffer.resize(bufferSize);
}

bool ELogQuantumTarget::start() {
    m_logThread = std::thread(&ELogQuantumTarget::logThread, this);
    return true;
}

bool ELogQuantumTarget::stop() {
    // send a poison pill to the log thread
    ELogRecord poison;
    poison.m_logMsg = (const char*)-1;
    log(poison);

    // now wait for log thread to finish
    m_logThread.join();
    return true;
}

void ELogQuantumTarget::log(const ELogRecord& logRecord) {
    if (!shouldLog(logRecord)) {
        return;
    }

    uint32_t writePos = m_writePos.fetch_add(1, std::memory_order_relaxed);
    uint32_t readPos = m_readPos.load(std::memory_order_relaxed);
    if (writePos - readPos < m_ringBuffer.size()) {
        ELogRecordData& recordData = m_ringBuffer[writePos % m_ringBuffer.size()];
        EntryState entryState = recordData.m_entryState.load(std::memory_order_relaxed);
        if (entryState == ES_VACANT) {
            if (recordData.m_entryState.compare_exchange_strong(entryState, ES_WRITING)) {
                recordData.m_logRecord = logRecord;
                recordData.m_logMsg = logRecord.m_logMsg;
                recordData.m_logRecord.m_logMsg = recordData.m_logMsg.c_str();
                recordData.m_entryState.store(ES_READY, std::memory_order_seq_cst);
            }
        }
    }
}

void ELogQuantumTarget::flush() {
    // log empty message, which designated a flush request
    // NOTE: there is no waiting for flush to complete
    ELogRecord dummy;
    dummy.m_logMsg = "";
    log(dummy);
}

void ELogQuantumTarget::logThread() {
    bool done = false;
    while (!done) {
        // get read/write pos
        uint32_t writePos = m_writePos.fetch_add(1, std::memory_order_relaxed);
        uint32_t readPos = m_readPos.load(std::memory_order_relaxed);

        // check if there is a new log record
        if (writePos > readPos) {
            // wait until record is ready for reading
            ELogRecordData& recordData = m_ringBuffer[readPos % m_ringBuffer.size()];
            EntryState entryState = recordData.m_entryState.load(std::memory_order_relaxed);
            while (entryState != ES_READY) {
                // TODO: cpu relax then try again
                entryState = recordData.m_entryState.load(std::memory_order_relaxed);
            }

            // set record in reading state
            assert(entryState == ES_READY);
            recordData.m_entryState.compare_exchange_strong(entryState, ES_READING,
                                                            std::memory_order_relaxed);

            // log record, flush or terminate
            if (recordData.m_logMsg == (const char*)-1) {
                done = true;
            } else if (recordData.m_logMsg.empty()) {
                m_logTarget->flush();
            } else {
                m_logTarget->log(recordData.m_logRecord);
            }

            // change state back to vacant and update read pos
            recordData.m_entryState.compare_exchange_strong(entryState, ES_VACANT,
                                                            std::memory_order_relaxed);
            m_readPos.fetch_add(1, std::memory_order_relaxed);
        }
    }

    // do a final flush and terminate
    m_logTarget->flush();
}

}  // namespace elog
