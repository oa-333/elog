#include "async/elog_quantum_target.h"

#include <cassert>

#include "elog_aligned_alloc.h"
#include "elog_common.h"
#include "elog_field_selector_internal.h"
#include "elog_report.h"

#define ELOG_FLUSH_REQUEST ((uint8_t)-1)
#define ELOG_STOP_REQUEST ((uint8_t)-2)

// TODO: add some backoff policy when queue is empty, to avoid tight loop when not needed
// TODO: consider CPU affinity for log thread for better performance

// TODO: allow quantum log target to specify in config what to do when queue is full:
// - wait until queue is ready (or even allow to give a timeout)
// - bail out immediately

// TODO: check again CPU relax and exponential backoff where needed

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogQuantumTarget)

ELOG_IMPLEMENT_LOG_TARGET(ELogQuantumTarget)

ELogQuantumTarget::ELogQuantumTarget(
    ELogTarget* logTarget, uint32_t bufferSize, uint64_t collectPeriodMicros /* = 0 */,
    CongestionPolicy congestionPolicy /* = CongestionPolicy::CP_WAIT */)
    : ELogAsyncTarget(logTarget),
      m_ringBuffer(nullptr),
      m_ringBufferSize(bufferSize),
      m_collectPeriodMicros(collectPeriodMicros),
      m_writePos(0),
      m_readPos(0) {}
// m_congestionPolicy(congestionPolicy)

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
    if (!m_subTarget->start()) {
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
    uint64_t dummy = 0;
    writeLogRecord(poison, dummy);

    // now wait for log thread to finish
    m_logThread.join();
    if (!m_subTarget->stop()) {
        ELOG_REPORT_ERROR("Quantum log target failed to stop underlying log target");
        return false;
    }
    if (m_ringBuffer != nullptr) {
        elogAlignedFreeObjectArray(m_bufferArray, m_ringBufferSize);
        elogAlignedFreeObjectArray(m_ringBuffer, m_ringBufferSize);
        m_ringBuffer = nullptr;
    }
    return true;
}

bool ELogQuantumTarget::writeLogRecord(const ELogRecord& logRecord, uint64_t& bytesWritten) {
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
    memcpy((void*)&recordData.m_logRecord, &logRecord, sizeof(ELogRecord));
    recordData.m_logBuffer->assign(logRecord.m_logMsg, logRecord.m_logMsgLen);
    recordData.m_logRecord.m_logMsg = recordData.m_logBuffer->getRef();
    recordData.m_entryState.store(ES_READY, std::memory_order_release);

    // NOTE: asynchronous loggers do not report bytes written
    bytesWritten = 0;
    return true;
}

bool ELogQuantumTarget::flushLogTarget() {
    // log empty message, which designated a flush request
    // NOTE: there is no waiting for flush to complete
    ELOG_CACHE_ALIGN ELogRecord flushRecord;
    flushRecord.m_logMsg = "";
    flushRecord.m_reserved = ELOG_FLUSH_REQUEST;
    uint64_t dummy = 0;
    writeLogRecord(flushRecord, dummy);
    return true;
}

void ELogQuantumTarget::logThread() {
    std::string threadName = std::string(getName()) + "-log-thread";
    setCurrentThreadNameField(threadName.c_str());
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
            // NOTE: it is possible for writers to grab slots beyond the total available in the ring
            // buffer, such that writePos - readPos > ring-buffer-size - for this reason we must
            // restrict writePos so that it does not surpass the size limit, otherwise the reader
            // will do full round and start looking at entries it has already marked as VACANT
            if (writePos - readPos > m_ringBufferSize) {
                writePos = readPos + m_ringBufferSize;
            }

            // wait until record is ready for reading
            ELogRecordData& recordData = m_ringBuffer[readPos % m_ringBufferSize];
            EntryState entryState = recordData.m_entryState.load(std::memory_order_relaxed);
            // uint32_t localSpinCount = SPIN_COUNT_INIT;
            while (entryState != ES_READY) {
                // cpu relax then try again
                // NOTE: this degrades performance, not clear yet why
                // spin and exponential backoff
                // for (uint32_t spin = 0; spin < localSpinCount; ++spin) {
                //    CPU_RELAX;
                //}
                // localSpinCount *= 2;
                entryState = recordData.m_entryState.load(std::memory_order_relaxed);
                // we don't spin/back-off here since the state change is expected to happen
                // immediately
            }

            // no need to move state to reading
            assert(recordData.m_entryState.load(std::memory_order_relaxed) == ES_READY);

            // log record, flush or terminate
            if (recordData.m_logRecord.m_reserved == ELOG_STOP_REQUEST) {
                done = true;
            } else if (recordData.m_logRecord.m_reserved == ELOG_FLUSH_REQUEST) {
                m_subTarget->flush();
            } else {
                m_subTarget->log(recordData.m_logRecord);
            }

            // change state back to vacant and update read pos
            recordData.m_entryState.store(ES_VACANT, std::memory_order_relaxed);
            m_readPos.fetch_add(1, std::memory_order_relaxed);
        } else {
            // write pos is not changing yet, so this mostly means the writers are idle, but since
            // they might start any time, we don't do any spin/backoff, but rather just relax
            // NOTE: this degrades performance, not clear yet why
            if (m_collectPeriodMicros == 0) {
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
            } else {
                std::this_thread::sleep_for(std::chrono::microseconds(m_collectPeriodMicros));
            }
        }
    }

    // do a final flush and terminate
    m_subTarget->flush();
}

}  // namespace elog
