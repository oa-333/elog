#include "elog_quantum_target.h"

#include <cassert>

#include "elog_system.h"

#ifdef ELOG_GCC
#define CPU_RELAX asm volatile("pause\n" : : : "memory")
#elif defined(ELOG_MSVC)
#define CPU_RELAX YieldProcessor()
#else
#define CPU_RELAX
#endif

namespace elog {

ELogQuantumTarget::ELogQuantumTarget(
    ELogTarget* logTarget, uint32_t bufferSize,
    CongestionPolicy congestionPolicy /* = CongestionPolicy::CP_WAIT */)
    : ELogTarget("quantum"),
      m_writePos(0),
      m_readPos(0),
      m_congestionPolicy(congestionPolicy),
      m_logTarget(logTarget),
      m_stop(false) {
    m_ringBuffer.resize(bufferSize);
}

bool ELogQuantumTarget::startLogTarget() {
    if (!m_logTarget->start()) {
        return false;
    }
    m_logThread = std::thread(&ELogQuantumTarget::logThread, this);
    return true;
}

bool ELogQuantumTarget::stopLogTarget() {
    // send a poison pill to the log thread
    ELogRecord poison;
    poison.m_logMsg = (const char*)-1;
    log(poison);

    // now wait for log thread to finish
    m_logThread.join();
    uint32_t writePos = m_writePos.load(std::memory_order_relaxed);
    uint32_t readPos = m_readPos.load(std::memory_order_relaxed);
    return m_logTarget->stop();
}

void ELogQuantumTarget::log(const ELogRecord& logRecord) {
    if (!shouldLog(logRecord)) {
        return;
    }

    uint32_t writePos = m_writePos.fetch_add(1, std::memory_order_acquire);
    uint32_t readPos = m_readPos.load(std::memory_order_relaxed);

    // wait until there is no other writer contending for the same entry
    while (writePos - readPos >= m_ringBuffer.size()) {
        CPU_RELAX;
        readPos = m_readPos.load(std::memory_order_relaxed);
    }
    ELogRecordData& recordData = m_ringBuffer[writePos % m_ringBuffer.size()];
    EntryState entryState = recordData.m_entryState.load(std::memory_order_seq_cst);

    // now wait for entry to become vacant
    while (entryState != ES_VACANT) {
        CPU_RELAX;
        entryState = recordData.m_entryState.load(std::memory_order_relaxed);
    }
    assert(entryState == ES_VACANT);

    recordData.m_entryState.store(ES_WRITING, std::memory_order_seq_cst);
    recordData.m_logRecord = logRecord;
    if (logRecord.m_logMsg != (const char*)-1) {
        // make a copy of the log message and update pointer to point to msg copy
        recordData.m_logMsg = logRecord.m_logMsg;
        recordData.m_logRecord.m_logMsg = recordData.m_logMsg.c_str();
    }
    recordData.m_entryState.store(ES_READY, std::memory_order_release);
}

void ELogQuantumTarget::flush() {
    // log empty message, which designated a flush request
    // NOTE: there is no waiting for flush to complete
    ELogRecord dummy;
    dummy.m_logMsg = nullptr;
    log(dummy);
}

void ELogQuantumTarget::logThread() {
    bool done = false;
    while (!done) {
        // get read/write pos
        volatile uint32_t writePos = m_writePos.load(std::memory_order_relaxed);
        volatile uint32_t readPos = m_readPos.load(std::memory_order_relaxed);

        // check if there is a new log record
        if (writePos > readPos) {
            // wait until record is ready for reading
            ELogRecordData& recordData = m_ringBuffer[readPos % m_ringBuffer.size()];
            EntryState entryState = recordData.m_entryState.load(std::memory_order_relaxed);
            while (entryState != ES_READY) {
                // cpu relax then try again
                // CPU_RELAX;
                entryState = recordData.m_entryState.load(std::memory_order_relaxed);
            }

            // set record in reading state
            assert(entryState == ES_READY);
            if (!recordData.m_entryState.compare_exchange_strong(entryState, ES_READING,
                                                                 std::memory_order_relaxed)) {
                assert(false);
            }

            // log record, flush or terminate
            if (recordData.m_logRecord.m_logMsg == (const char*)-1) {
                done = true;
            } else if (recordData.m_logRecord.m_logMsg == nullptr) {
                m_logTarget->flush();
            } else {
                m_logTarget->log(recordData.m_logRecord);
            }

            // change state back to vacant and update read pos
            recordData.m_entryState.store(ES_VACANT, std::memory_order_relaxed);
            m_readPos.fetch_add(1, std::memory_order_relaxed);
        } else {
            // relax before next round
            // CPU_RELAX;
        }
    }

    // do a final flush and terminate
    m_logTarget->flush();
}

}  // namespace elog
