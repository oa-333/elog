#include "elog_quantum_target.h"

#include <cassert>

#include "elog.h"
#include "elog_aligned_alloc.h"
#include "elog_common.h"
#include "elog_error.h"

#define ELOG_FLUSH_REQUEST ((uint16_t)-1)
#define ELOG_STOP_REQUEST ((uint16_t)-2)

namespace elog {

ELogQuantumTarget::ELogQuantumTarget(
    ELogTarget* logTarget, uint32_t bufferSize,
    CongestionPolicy congestionPolicy /* = CongestionPolicy::CP_WAIT */)
    : ELogAsyncTarget(logTarget),
      m_ringBuffer(nullptr),
      m_ringBufferSize(bufferSize),
      m_writePos(0),
      m_readPos(0),
      // m_congestionPolicy(congestionPolicy),
      m_stop(false) {}

bool ELogQuantumTarget::startLogTarget() {
    if (m_ringBuffer == nullptr) {
        m_ringBuffer =
            elogAlignedAllocObjectArray<ELogRecordData>(ELOG_CACHE_LINE, m_ringBufferSize);
        if (m_ringBuffer == nullptr) {
            ELOG_REPORT_ERROR(
                "Failed to allocate ring buffer of %u elements for quantum log target",
                m_ringBufferSize);
            return false;
        }
        m_bufferArray = elogAlignedAllocObjectArray<ELogBuffer>(ELOG_CACHE_LINE, m_ringBufferSize);
        if (m_bufferArray == nullptr) {
            ELOG_REPORT_ERROR(
                "Failed to allocate log buffer array of %u elements for quantum log target",
                m_ringBufferSize);
            elogAlignedFreeObjectArray(m_ringBuffer, m_ringBufferSize);
            return false;
        }
        //  reserve in advance some space to avoid penalty on first round
        for (uint32_t i = 0; i < m_ringBufferSize; ++i) {
            m_ringBuffer[i].setLogBuffer(&m_bufferArray[i]);
        }
    }
    if (!m_endTarget->start()) {
        elogAlignedFreeObjectArray(m_bufferArray, m_ringBufferSize);
        elogAlignedFreeObjectArray(m_ringBuffer, m_ringBufferSize);
        m_ringBuffer = nullptr;
        return false;
    }
    m_logThread = std::thread(&ELogQuantumTarget::logThread, this);
    return true;
}

bool ELogQuantumTarget::stopLogTarget() {
    // send a poison pill to the log thread
    ELOG_CACHE_ALIGN ELogRecord poison;
    poison.m_logMsg = "";
    poison.m_reserved = ELOG_STOP_REQUEST;
    writeLogRecord(poison);

    // now wait for log thread to finish
    m_logThread.join();
    if (!m_endTarget->stop()) {
        ELOG_REPORT_ERROR("Quantum log target failed to stop underlying log target");
        return false;
    }
    if (m_ringBuffer != nullptr) {
        // TODO: check again CPU relax and exponential backoff where needed
        elogAlignedFreeObjectArray(m_bufferArray, m_ringBufferSize);
        elogAlignedFreeObjectArray(m_ringBuffer, m_ringBufferSize);
        m_ringBuffer = nullptr;
    }
    return true;
}

uint32_t ELogQuantumTarget::writeLogRecord(const ELogRecord& logRecord) {
    uint64_t writePos = m_writePos.fetch_add(1, std::memory_order_acquire);
    uint64_t readPos = m_readPos.load(std::memory_order_relaxed);

    // wait until there is no other writer contending for the same entry
    while (writePos - readPos >= m_ringBufferSize) {
        CPU_RELAX;
        readPos = m_readPos.load(std::memory_order_relaxed);
    }
    ELogRecordData& recordData = m_ringBuffer[writePos % m_ringBufferSize];
    EntryState entryState = recordData.m_entryState.load(std::memory_order_seq_cst);

    // now wait for entry to become vacant
    while (entryState != ES_VACANT) {
        CPU_RELAX;
        entryState = recordData.m_entryState.load(std::memory_order_relaxed);
    }
    assert(entryState == ES_VACANT);

    recordData.m_entryState.store(ES_WRITING, std::memory_order_seq_cst);
    // recordData.m_logRecord = logRecord;
    memcpy(&recordData.m_logRecord, &logRecord, sizeof(ELogRecord));
    recordData.m_logBuffer->assign(logRecord.m_logMsg, logRecord.m_logMsgLen);
    recordData.m_logRecord.m_logMsg = recordData.m_logBuffer->getRef();
    recordData.m_entryState.store(ES_READY, std::memory_order_release);

    // NOTE: asynchronous loggers do not report bytes written
    // TODO: future statistics will record bytes submitted
    return 0;
}

void ELogQuantumTarget::flushLogTarget() {
    // log empty message, which designated a flush request
    // NOTE: there is no waiting for flush to complete
    ELOG_CACHE_ALIGN ELogRecord flushRecord;
    flushRecord.m_logMsg = "";
    flushRecord.m_reserved = ELOG_FLUSH_REQUEST;
    writeLogRecord(flushRecord);
}

void ELogQuantumTarget::logThread() {
    bool done = false;
    // const uint64_t SPIN_COUNT_INIT = 256;
    // const uint64_t SPIN_COUNT_MAX = 16384;
    // uint64_t spinCount = SPIN_COUNT_INIT;
    while (!done) {
        // get read/write pos
        uint64_t writePos = m_writePos.load(std::memory_order_relaxed);
        uint64_t readPos = m_readPos.load(std::memory_order_relaxed);

        // check if there is a new log record
        if (writePos > readPos) {
            // wait until record is ready for reading
            ELogRecordData& recordData = m_ringBuffer[readPos % m_ringBufferSize];
            EntryState entryState = recordData.m_entryState.load(std::memory_order_relaxed);
            // uint32_t localSpinCount = SPIN_COUNT_INIT;
            while (entryState != ES_READY) {
                // cpu relax then try again
                // NOTE: this degrades performance, not clear yet why
                // CPU_RELAX;
                entryState = recordData.m_entryState.load(std::memory_order_relaxed);
                // we don't spin/back-off here since the state change is expected to happen
                // immediately

                // spin and exponential backoff
                // for (uint32_t spin = 0; spin < localSpinCount; ++spin);
                // localSpinCount *= 2;
            }

            // set record in reading state
            assert(entryState == ES_READY);
            if (!recordData.m_entryState.compare_exchange_strong(entryState, ES_READING,
                                                                 std::memory_order_relaxed)) {
                assert(false);
            }

            // log record, flush or terminate
            if (recordData.m_logRecord.m_reserved == ELOG_STOP_REQUEST) {
                done = true;
            } else if (recordData.m_logRecord.m_reserved == ELOG_FLUSH_REQUEST) {
                m_endTarget->flush();
            } else {
                m_endTarget->log(recordData.m_logRecord);
            }

            // change state back to vacant and update read pos
            recordData.m_entryState.store(ES_VACANT, std::memory_order_relaxed);
            m_readPos.fetch_add(1, std::memory_order_relaxed);
        } else {
            // write pos is not changing yet, so this mostly means the writers are idle, but since
            // they might start any time, we don't do any spin/backoff, but rather just relax
            // NOTE: this degrades performance, not clear yet why
            // CPU_RELAX;
            /*if (spinCount > SPIN_COUNT_MAX) {
                // yield processor
                std::this_thread::yield();
            } else {
                // relax before next round
                CPU_RELAX;
                // spin and do exponential backoff
                for (uint32_t spin = 0; spin < spinCount; ++spin);
                spinCount *= 2;
            }*/
        }
    }

    // do a final flush and terminate
    m_endTarget->flush();
}

}  // namespace elog
