#include "elog_multi_quantum_target.h"

#include <cassert>
#include <cinttypes>

#include "elog_aligned_alloc.h"
#include "elog_common.h"
#include "elog_field_selector_internal.h"
#include "elog_internal.h"
#include "elog_report.h"
#include "elog_tls.h"

#define ELOG_FLUSH_REQUEST ((uint8_t)-1)
#define ELOG_STOP_REQUEST ((uint8_t)-2)

// TODO: add some backoff policy when queue is empty, to avoid tight loop when not needed
// TODO: consider CPU affinity for log thread for better performance

// TODO: allow quantum log target to specify in config what to do when queue is full:
// - wait until queue is ready (or even allow to give a timeout)
// - bail out immediately

// TODO: check again CPU relax and exponential backoff where needed

#define ELOG_INVALID_THREAD_SLOT_ID ((uint64_t)-1)
#define ELOG_NO_THREAD_SLOT_ID ((uint64_t)-2)
#define WORD_BIT_SIZE 64

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogMultiQuantumTarget)

static ELogTlsKey sThreadSlotKey = ELOG_INVALID_TLS_KEY;
static thread_local uint64_t sThreadSlotId = ELOG_INVALID_THREAD_SLOT_ID;

typedef std::pair<ELogMultiQuantumTarget*, uint64_t> CleanupPair;

void ELogMultiQuantumTarget::cleanupThreadSlot(void* key) {
    CleanupPair* cleanupPair = (CleanupPair*)key;
    if (cleanupPair == nullptr) {
        ELOG_REPORT_WARN("Null multi-quantum target cleanup pair in cleanupThreadSlot()");
        return;
    }
    if (cleanupPair->first == nullptr) {
        ELOG_REPORT_WARN(
            "Null multi-quantum target pointer in cleanup pair in cleanupThreadSlot()");
        delete cleanupPair;
        return;
    }

    cleanupPair->first->releaseThreadSlot(cleanupPair->second);
    delete cleanupPair;
}

ELogMultiQuantumTarget::ELogMultiQuantumTarget(
    ELogTarget* logTarget, uint32_t ringBufferSize,
    uint32_t readerCount /* = ELOG_DEFAULT_READER_COUNT */,
    uint32_t activeRevisitPeriod /* = ELOG_DEFAULT_ACTIVE_REVISIT_COUNT */,
    uint32_t fullRevisitPeriod /* = ELOG_DEFAULT_FULL_REVISIT_COUNT */,
    uint32_t maxBatchSize /* = ELOG_MQT_DEFAULT_MAX_BATCH_SIZE */,
    uint64_t collectPeriodMicros /* = ELOG_DEFAULT_COLLECT_PERIOD_MICROS */,
    CongestionPolicy congestionPolicy /* = CongestionPolicy::CP_WAIT */)
    : ELogAsyncTarget(logTarget),
      m_ringBuffers(nullptr),
      m_activeThreads(nullptr),
      m_activeRingBuffers(nullptr),
      m_threadLogTime(nullptr),
      m_recentThreadLogTime(nullptr),
      m_maxThreadCount(elog::getMaxThreads()),
      m_bitsetSize(0),
      m_ringBufferSize(ringBufferSize),
      m_readerCount(readerCount),
      m_activeRevisitPeriod(activeRevisitPeriod),
      m_fullRevisitPeriod(fullRevisitPeriod),
      m_maxBatchSize(maxBatchSize),
      m_collectPeriodMicros(collectPeriodMicros) {
    m_bitsetSize = (m_maxThreadCount + WORD_BIT_SIZE - 1) / WORD_BIT_SIZE * WORD_BIT_SIZE;
    m_sortingFunnelSize = m_ringBufferSize * m_maxThreadCount;
}
// m_congestionPolicy(congestionPolicy)

bool ELogMultiQuantumTarget::startLogTarget() {
    if (!m_sortingFunnel.initialize(m_sortingFunnelSize)) {
        ELOG_REPORT_ERROR("Failed to initialize sorting funnel in multi-quantum log target");
        return false;
    }

    // create TLS key (for slot cleanup)
    if (!elogCreateTls(sThreadSlotKey, cleanupThreadSlot)) {
        ELOG_REPORT_ERROR(
            "Cannot create multi-quantum log target, failed to allocate TLS key for thread slot "
            "cleanup");
        cleanup();
        return false;
    }

    // create ring buffer array
    if (m_ringBuffers == nullptr) {
        m_ringBuffers = elogAlignedAllocObjectArray<RingBuffer>(ELOG_CACHE_LINE, m_maxThreadCount);
        if (m_ringBuffers == nullptr) {
            ELOG_REPORT_ERROR("Failed to allocate %" PRIu64
                              " ring buffers for multi-quantum log target",
                              m_maxThreadCount);
            cleanup();
            return false;
        }
        for (uint64_t i = 0; i < m_maxThreadCount; ++i) {
            if (!m_ringBuffers[i].initialize(m_ringBufferSize)) {
                cleanup();
                return false;
            }
        }
    }

    // allocate bitset arrays
    if (m_activeThreads == nullptr) {
        m_activeThreads =
            elogAlignedAllocObjectArray<std::atomic<uint64_t>>(ELOG_CACHE_LINE, m_bitsetSize, 0ull);
        if (m_activeThreads == nullptr) {
            ELOG_REPORT_ERROR("Failed to allocate %" PRIu64
                              " words for active-threads bitset array for %" PRIu64
                              " threads in multi-quantum log target",
                              m_bitsetSize, m_maxThreadCount);
            cleanup();
            return false;
        }
    }

    if (m_activeRingBuffers == nullptr) {
        m_activeRingBuffers =
            elogAlignedAllocObjectArray<std::atomic<uint64_t>>(ELOG_CACHE_LINE, m_bitsetSize, 0ull);
        if (m_activeRingBuffers == nullptr) {
            ELOG_REPORT_ERROR("Failed to allocate %" PRIu64
                              " words for active ring buffers bitset array for %" PRIu64
                              " threads in multi-quantum log target",
                              m_bitsetSize, m_maxThreadCount);
            cleanup();
            return false;
        }
    }

    if (m_threadLogTime == nullptr) {
        m_threadLogTime = elogAlignedAllocObjectArray<std::atomic<uint64_t>>(
            ELOG_CACHE_LINE, m_maxThreadCount, 0ull);
        if (m_threadLogTime == nullptr) {
            ELOG_REPORT_ERROR("Failed to allocate %" PRIu64
                              " timestamps for readers in multi-quantum log target",
                              m_maxThreadCount);
            cleanup();
            return false;
        }
    }

    if (m_recentThreadLogTime == nullptr) {
        m_recentThreadLogTime =
            elogAlignedAllocObjectArray<uint64_t>(ELOG_CACHE_LINE, m_maxThreadCount, 0ull);
        if (m_recentThreadLogTime == nullptr) {
            ELOG_REPORT_ERROR("Failed to allocate %" PRIu64
                              " timestamps for readers in multi-quantum log target",
                              m_maxThreadCount);
            cleanup();
            return false;
        }
    }

    // NOTE: thread ring buffers are initialized on demand

    // start destination target
    if (!m_subTarget->start()) {
        cleanup();
        return false;
    }

    // launch sorting thread
    m_sortingThread = std::thread(&ELogMultiQuantumTarget::sortingThread, this);

    // launch reader threads, each reader takes a portion of threads
    // TODO: add policy to determine how to distribute new thread slots among readers
    uint64_t wordsPerReader = m_bitsetSize / m_readerCount;
    for (uint64_t i = 0; i < m_readerCount; ++i) {
        uint64_t fromWordIndex = i * wordsPerReader;
        uint64_t toWordIndex = fromWordIndex + wordsPerReader;
        if (i + 1 == m_readerCount) {
            toWordIndex = m_bitsetSize;
        }
        m_readerThreads.emplace_back(&ELogMultiQuantumTarget::readerThread, this, i, fromWordIndex,
                                     toWordIndex);
    }
    return true;
}

bool ELogMultiQuantumTarget::stopLogTarget() {
    // send a poison pill to all reader threads
    ELOG_CACHE_ALIGN ELogRecord poison;
    poison.m_logMsg = "";
    poison.m_reserved = ELOG_STOP_REQUEST;
    uint64_t ringBuffersPerReader = m_maxThreadCount / m_readerCount;
    for (uint64_t i = 0; i < m_readerCount; ++i) {
        // choose any ring buffer that belongs to the reader
        uint64_t slotId = i * ringBuffersPerReader;
        m_ringBuffers[slotId].writeLogRecord(poison);
    }

    // now wait for log thread to finish
    for (uint64_t i = 0; i < m_readerCount; ++i) {
        m_readerThreads[i].join();
    }

    // TODO: stop sorting thread
    m_sortingThread.join();

    // stop the destination target
    if (!m_subTarget->stop()) {
        ELOG_REPORT_ERROR("Quantum log target failed to stop underlying log target");
        return false;
    }

    cleanup();
    return true;
}

uint32_t ELogMultiQuantumTarget::writeLogRecord(const ELogRecord& logRecord) {
    // obtain slot if needed
    uint64_t slotId = getThreadSlotId();
    if (slotId == ELOG_INVALID_THREAD_SLOT_ID) {
        return 0;
    }

    // write log record to slot
    m_ringBuffers[slotId].writeLogRecord(logRecord);
    raiseRingBufferBit(slotId);

    // NOTE: asynchronous loggers do not report bytes written
    return 0;
}

bool ELogMultiQuantumTarget::flushLogTarget() {
    // log empty message, which designated a flush request
    // NOTE: there is no waiting for flush to complete
    ELOG_CACHE_ALIGN ELogRecord flushRecord;
    flushRecord.m_logMsg = "";
    flushRecord.m_reserved = ELOG_FLUSH_REQUEST;
    writeLogRecord(flushRecord);
    return true;
}

bool ELogMultiQuantumTarget::extractToSortingFunnel(RingBuffer* ringBuffer, uint64_t& maxTimeStamp,
                                                    bool& isValid, bool& extractedAllRecords) {
    bool isDone = false;
    ELogRecord logRecord = {};
    uint64_t msgCount = 0;
    ELogBuffer logBuffer;
    extractedAllRecords = false;
    while (msgCount < m_maxBatchSize && !isDone && !extractedAllRecords) {
        if (ringBuffer->readLogRecord(logRecord, logBuffer)) {
            // NOTE: flush records are handled by the sorting thread, so that access to the
            // destination target is single-thread and can avoid using a lock
            if (logRecord.m_reserved == ELOG_STOP_REQUEST) {
                // poison record received, so we stop (but still propagate the poison to the sorting
                // thread)
                m_sortingFunnel.writeLogRecord(logRecord);
                isDone = true;
            } else {
                logRecord.m_logMsg = logBuffer.getRef();
                m_sortingFunnel.writeLogRecord(logRecord);
                ++msgCount;
            }
            logBuffer.reset();
        } else {
            extractedAllRecords = true;
        }
    }
    if (msgCount > 0) {
        maxTimeStamp = elogTimeToInt64(logRecord.m_logTime);
        ELOG_REPORT_TRACE("Reader extracted %" PRIu64
                          " messages from ring buffer with recent timestamp %" PRIu64,
                          msgCount, maxTimeStamp);
        isValid = true;
        m_readCount.fetch_add(msgCount, std::memory_order_relaxed);
    } else {
        isValid = false;
    }
    return isDone;
}

void ELogMultiQuantumTarget::readerThread(uint64_t readerId, uint64_t fromWordIndex,
                                          uint64_t toWordIndex) {
    // read from all active words in the region of this reader
    std::string tname = std::string("reader-") + std::to_string(readerId);
    setCurrentThreadNameField(tname.c_str());
    uint64_t iterationCounter = 0;
    bool done = false;
    while (!done) {
        bool activeRevisit = false;
        bool fullRevisit = false;
        ++iterationCounter;
        if (iterationCounter % m_fullRevisitPeriod == 0) {
            fullRevisit = true;
        } else if (iterationCounter % m_activeRevisitPeriod) {
            activeRevisit = true;
        }

        // the indices are of active threads full words
        for (uint64_t i = fromWordIndex; i < toWordIndex && !done; ++i) {
            if (fullRevisit) {
                // visit all threads, whether active or not, regardless of ring buffer bit
                done = revisitAllThreads(i);
            } else if (activeRevisit) {
                // visit all active threads, even if ring buffer bit is not raised
                done = revisitAllActiveThreads(i);
            } else {
                // read only from active ring buffers
                done = visitActiveRingBuffers(i);
            }
        }
    }
}

bool ELogMultiQuantumTarget::visitActiveRingBuffers(uint64_t wordIndex) {
    bool done = false;
    uint64_t word = m_activeRingBuffers[wordIndex].load(std::memory_order_acquire);
    while (word != 0 && !done) {
        uint64_t offset = (uint64_t)std::countr_zero(word);
        uint64_t slotId = wordIndex * WORD_BIT_SIZE + offset;
        assert(slotId < m_maxThreadCount);
        word &= ~(1 << offset);
        done = readThreadRingBuffer(slotId);
    }
    return done;
}

bool ELogMultiQuantumTarget::revisitAllActiveThreads(uint64_t wordIndex) {
    bool done = false;
    for (uint64_t j = 0; j < WORD_BIT_SIZE && !done; ++j) {
        uint64_t slotId = wordIndex * WORD_BIT_SIZE + j;
        if (slotId >= m_maxThreadCount) {
            break;
        }
        if (isThreadActive(slotId)) {
            done = readThreadRingBuffer(slotId);
        }
    }
    return done;
}

bool ELogMultiQuantumTarget::revisitAllThreads(uint64_t wordIndex) {
    bool done = false;
    for (uint64_t j = 0; j < WORD_BIT_SIZE && !done; ++j) {
        uint64_t slotId = wordIndex * WORD_BIT_SIZE + j;
        if (slotId >= m_maxThreadCount) {
            break;
        }
        done = readThreadRingBuffer(slotId);
    }
    return done;
}

bool ELogMultiQuantumTarget::readThreadRingBuffer(uint64_t slotId) {
    bool isValid = false;
    bool extractedAllRecords = false;
    uint64_t timeStamp = 0;
    bool done =
        extractToSortingFunnel(&m_ringBuffers[slotId], timeStamp, isValid, extractedAllRecords);
    if (extractedAllRecords) {
        resetRingBufferBit(slotId);
    }
    if (!done && isValid) {
        m_threadLogTime[slotId].store(timeStamp, std::memory_order_relaxed);
        ELOG_REPORT_TRACE("Thread %" PRIu64 " timestamp advanced to %" PRIu64, slotId, timeStamp);
    }
    return done;
}

void ELogMultiQuantumTarget::sortingThread() {
    // this is the sorting thread that sorts a sliding window of the sorting funnel
    // at each iteration the max timestamp is checked, and then the prefix of the
    setCurrentThreadNameField("sorting-thread");
    bool done = false;
    uint64_t prevMinTimeStamp = 0;
    while (!done) {
        uint64_t minTimeStamp = 0;
        bool isValid = getMinTimeStamp(minTimeStamp);
        if (!isValid || (prevMinTimeStamp == minTimeStamp)) {
            std::this_thread::sleep_for(std::chrono::microseconds(0 * m_collectPeriodMicros));
            continue;
        }
        ELOG_REPORT_DEBUG("Min time stamp advanced to %" PRIu64, minTimeStamp);
        prevMinTimeStamp = minTimeStamp;

        // get ring buffer read/write position
        uint64_t readPos = m_sortingFunnel.m_readPos.load(std::memory_order_relaxed);
        uint64_t endPos = m_sortingFunnel.m_writePos.load(std::memory_order_relaxed);
        if (endPos > readPos) {
            // NOTE: it is possible for writers to grab slots beyond the total available in the ring
            // buffer, such that endPos - readPos > ring-buffer-size - for this reason we must
            // restrict endPos so that it does not surpass the size limit, otherwise the reader will
            // do full round and start looking at entries it has already marked as VACANT
            if (endPos - readPos > m_sortingFunnelSize) {
                endPos = readPos + m_sortingFunnelSize;
            }
            ELOG_REPORT_TRACE("Sorting thread checking range [%" PRIu64 "-%" PRIu64 "]", readPos,
                              endPos);
            m_funnelCount.store(endPos, std::memory_order_relaxed);

            // we need to wait until all entries stabilize
            // NOTE: in the meantime more records may be added and that's ok
            waitFunnelRangeStable(readPos, endPos);
            ELOG_REPORT_TRACE("Range [%" PRIu64 "-%" PRIu64 "] is stable", readPos, endPos);
            m_stableCount.store(endPos, std::memory_order_relaxed);

            // now sort from the beginning until end pos bu timestamp, thread id is tie breaker
            // NOTE: in the meantime more records may be added and that's ok
            sortFunnel(readPos, endPos);
            ELOG_REPORT_TRACE("Range [%" PRIu64 "-%" PRIu64 "] sorted", readPos, endPos);
            m_sortCount.store(endPos, std::memory_order_relaxed);

            // now process all records up to max time stamp
            done = shipReadySortedRecords(readPos, endPos, minTimeStamp);
        }
    }

    // do a final flush and terminate
    m_subTarget->flush();
}

bool ELogMultiQuantumTarget::getMinTimeStamp(uint64_t& minTimeStamp) {
    // TODO: when test is over a thread becomes in active, so we can't get min timestamp.
    // also when some threads are not writing log records, their time stamp may not be updated, but
    // that is ok
    //

    // make sure we have all active threads have reported at least once
    bool isValid = false;
    bool allDormant = true;
    minTimeStamp = UINT64_MAX;

    // time stamp in case all threads are not active (must taken before checking threads, see below)
    ELogTime logTime;
    elogGetCurrentTime(logTime);

    for (uint64_t i = 0; i < m_maxThreadCount; ++i) {
        // we get time stamp anyway, since sometimes a thread has already terminated, but it has
        // more records pending to be processed
        uint64_t timeStamp = m_threadLogTime[i].load(std::memory_order_relaxed);
        if (timeStamp == 0) {
            if (isThreadActive(i)) {
                // this is definitely not a valid minimum timestamp reading:
                // NOTE: a threads just grabbed a slot but has not written yet the log record, so we
                // need to wait for the next round. This is not dangerous since the thread is grabs
                // a slot only if it is about to write a log record for the first time, so we will
                // not get stuck here waiting for something that will never happen
                return false;
            } else {
                // slot has never been used, so skip it
                continue;
            }
        }
        minTimeStamp = std::min(minTimeStamp, timeStamp);

        // at least one active thread, so it can be valid
        isValid = true;

        // check if timestamp advanced for this thread since last round
        if (timeStamp > m_recentThreadLogTime[i]) {
            allDormant = false;
            m_recentThreadLogTime[i] = timeStamp;
        }
    }

    // NOTE: if all threads are dormant, then all records can be sorted an processed until the
    // current time, but the time must be taken before we check all thread state, because by the
    // time we finish checking all threads, some of them might have sent some log records
    if (isValid && allDormant) {
        minTimeStamp = elogTimeToInt64(logTime);
        ELOG_REPORT_DEBUG("ALl active threads are dormant, reporting min timestamp as: %" PRIu64,
                          minTimeStamp);
    }
    return isValid;
}

void ELogMultiQuantumTarget::waitFunnelRangeStable(uint64_t readPos, uint64_t endPos) {
    while (readPos < endPos) {
        uint64_t index = readPos % m_sortingFunnelSize;
        ELogRecordData& recordData = *m_sortingFunnel.m_recordArray[index];
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

        assert(entryState == ES_READY);
        ++readPos;
    }
}

void ELogMultiQuantumTarget::sortFunnel(uint64_t readPos, uint64_t endPos) {
    // sort on a range that might cross array boundary and wrap around
    uint64_t readIndex = readPos % m_sortingFunnelSize;
    uint64_t endIndex = endPos % m_sortingFunnelSize;
    if (readIndex < endIndex) {
        // do stable sort so that records from the same thread will keep their order
        std::stable_sort(&m_sortingFunnel.m_recordArray[readIndex],
                         &m_sortingFunnel.m_recordArray[endIndex], isRecordDataLess);
    } else {
        // use special iterators
        SortingFunnelIterator itr1(&m_sortingFunnel, m_sortingFunnelSize, readPos);
        SortingFunnelIterator itr2(&m_sortingFunnel, m_sortingFunnelSize, endPos);
        // do stable sort so that records from the same thread will keep their order
        std::stable_sort(itr1, itr2, [](const ELogRecordData* lhs, const ELogRecordData* rhs) {
            return isRecordDataLess(lhs, rhs);
        });
    }
}

bool ELogMultiQuantumTarget::isRecordDataLess(const ELogRecordData* lhs,
                                              const ELogRecordData* rhs) {
    uint64_t lhsTime = elogTimeToUnixTimeNanos(lhs->m_logRecord.m_logTime);
    uint64_t rhsTime = elogTimeToUnixTimeNanos(rhs->m_logRecord.m_logTime);
    if (lhsTime < rhsTime) {
        return true;
    }
    if (lhsTime > rhsTime) {
        return false;
    }
    if (lhs->m_logRecord.m_threadId < rhs->m_logRecord.m_threadId) {
        return true;
    }
    if (lhs->m_logRecord.m_threadId > rhs->m_logRecord.m_threadId) {
        return false;
    }
    return lhs->m_logRecord.m_logRecordId < rhs->m_logRecord.m_logRecordId;
}

bool ELogMultiQuantumTarget::shipReadySortedRecords(uint64_t readPos, uint64_t endPos,
                                                    uint64_t minTimeStamp) {
    bool done = false;
    uint64_t msgCount = 0;
    ELOG_REPORT_TRACE("Shipping log records of range [%" PRIu64 "-%" PRIu64
                      "], by time stamp limit %" PRIu64,
                      readPos, endPos, minTimeStamp);
    while (readPos < endPos && !done) {
        uint64_t index = readPos % m_sortingFunnelSize;
        ELogRecordData& recordData = *m_sortingFunnel.m_recordArray[index];

        // no need to move state to reading
        assert(recordData.m_entryState.load(std::memory_order_relaxed) == ES_READY);

        // first check special records
        if (recordData.m_logRecord.m_reserved == ELOG_STOP_REQUEST) {
            done = true;
        } else if (recordData.m_logRecord.m_reserved == ELOG_FLUSH_REQUEST) {
            m_subTarget->flush();
            ELOG_REPORT_TRACE("Flush issued");
        } else {
            // now check log time
            uint64_t logTime = elogTimeToInt64(recordData.m_logRecord.m_logTime);
            if (logTime < minTimeStamp) {
                m_subTarget->log(recordData.m_logRecord);
                ++msgCount;
            } else {
                ELOG_REPORT_TRACE("Stopped shipping at read pos %" PRIu64
                                  " with time stamp %" PRIu64,
                                  readPos, logTime);
                break;
            }
        }

        // change state back to vacant and update read pos
        recordData.m_entryState.store(ES_VACANT, std::memory_order_relaxed);
        m_sortingFunnel.m_readPos.fetch_add(1, std::memory_order_relaxed);
        ++readPos;
    }
    m_shipCount.store(readPos, std::memory_order_relaxed);

    ELOG_REPORT_TRACE("Sorting funnel shipped %" PRIu64 " messages, readPos is at %" PRIu64,
                      msgCount, readPos);

    return done;
}

uint64_t ELogMultiQuantumTarget::getThreadSlotId() {
    // obtain slot if needed
    if (sThreadSlotId == ELOG_INVALID_THREAD_SLOT_ID) {
        sThreadSlotId = obtainThreadSlot();
        if (sThreadSlotId == ELOG_INVALID_THREAD_SLOT_ID) {
            ELOG_REPORT_ERROR(
                "Cannot write log record, cannot obtain slot for current thread, all slots are "
                "used");
            // mark as already attempted
            sThreadSlotId = ELOG_NO_THREAD_SLOT_ID;
            return ELOG_INVALID_THREAD_SLOT_ID;
        }

        CleanupPair* cleanupPair = new (std::nothrow) CleanupPair(this, sThreadSlotId);
        if (cleanupPair == nullptr) {
            ELOG_REPORT_ERROR(
                "Cannot allocate cleanup pair for thread slot in multi-quantum log target, out "
                "of memory");
            releaseThreadSlot(sThreadSlotId);
            sThreadSlotId = ELOG_NO_THREAD_SLOT_ID;
            return ELOG_INVALID_THREAD_SLOT_ID;
        }

        if (!elogSetTls(sThreadSlotKey, cleanupPair)) {
            ELOG_REPORT_ERROR("Failed to store slot id for cleanup in multi-quantum log target");
            releaseThreadSlot(sThreadSlotId);
            sThreadSlotId = ELOG_NO_THREAD_SLOT_ID;
            return ELOG_INVALID_THREAD_SLOT_ID;
        }

        // raise bit, may encounter a bit of resistance
        raiseThreadBit(sThreadSlotId);
    }

    // if already tried once we back off
    if (sThreadSlotId == ELOG_NO_THREAD_SLOT_ID) {
        return ELOG_INVALID_THREAD_SLOT_ID;
    }

    return sThreadSlotId;
}

uint64_t ELogMultiQuantumTarget::obtainThreadSlot() {
    for (uint64_t i = 0; i < m_maxThreadCount; ++i) {
        uint64_t isUsed = m_ringBuffers[i].m_isUsed.load(std::memory_order_acquire);
        if (!isUsed && m_ringBuffers[i].m_isUsed.compare_exchange_strong(
                           isUsed, 1, std::memory_order_release)) {
            return i;
        }
    }

    return ELOG_INVALID_THREAD_SLOT_ID;
}

void ELogMultiQuantumTarget::releaseThreadSlot(uint64_t slotId) {
    if (slotId >= m_maxThreadCount) {
        ELOG_REPORT_ERROR("Invalid slot id %" PRIu64
                          " for cleanup in multi-quantum target, out of range",
                          slotId);
        return;
    }
    resetThreadBit(slotId);

    // NOTE: other thread can continue in this slot while the reader has not finished reading
    // previous thread's log records
    m_ringBuffers[slotId].m_isUsed.store(0, std::memory_order_relaxed);
}

void ELogMultiQuantumTarget::raiseBit(std::atomic<uint64_t>* bitset, uint64_t slotId) {
    uint64_t index = slotId / WORD_BIT_SIZE;
    uint64_t offset = slotId % WORD_BIT_SIZE;
    uint64_t word = bitset[index].load(std::memory_order_relaxed);
    uint64_t newWord = word | (1ull << offset);
    while (!bitset[index].compare_exchange_strong(word, newWord, std::memory_order_seq_cst)) {
        word = bitset[index].load(std::memory_order_relaxed);
        newWord = word | (1ull << offset);
    }
}

void ELogMultiQuantumTarget::resetBit(std::atomic<uint64_t>* bitset, uint64_t slotId) {
    uint64_t index = slotId / WORD_BIT_SIZE;
    uint64_t offset = slotId % WORD_BIT_SIZE;
    uint64_t word = bitset[index].load(std::memory_order_relaxed);
    uint64_t newWord = word & ~(1ull << offset);
    while (!bitset[index].compare_exchange_strong(word, newWord, std::memory_order_seq_cst)) {
        word = bitset[index].load(std::memory_order_relaxed);
        newWord = word & ~(1ull << offset);
    }
}

void ELogMultiQuantumTarget::cleanup() {
    if (m_ringBuffers != nullptr) {
        for (uint64_t i = 0; i < m_maxThreadCount; ++i) {
            m_ringBuffers[i].terminate();
        }
        elogAlignedFreeObjectArray(m_ringBuffers, m_maxThreadCount);
        m_ringBuffers = nullptr;
    }

    if (m_recentThreadLogTime != nullptr) {
        elogAlignedFreeObjectArray(m_recentThreadLogTime, m_maxThreadCount);
        m_recentThreadLogTime = nullptr;
    }

    if (m_threadLogTime != nullptr) {
        elogAlignedFreeObjectArray(m_threadLogTime, m_maxThreadCount);
        m_threadLogTime = nullptr;
    }

    if (m_activeThreads != nullptr) {
        elogAlignedFreeObjectArray(m_activeThreads, m_bitsetSize);
        m_activeThreads = nullptr;
    }

    if (m_activeRingBuffers != nullptr) {
        elogAlignedFreeObjectArray(m_activeRingBuffers, m_bitsetSize);
        m_activeRingBuffers = nullptr;
    }

    elogDestroyTls(sThreadSlotKey);
    m_sortingFunnel.terminate();
}

bool ELogMultiQuantumTarget::RingBuffer::initialize(uint64_t ringBufferSize) {
    //  reserve in advance some space to avoid penalty on first round
    if (m_recordArray == nullptr) {
        m_recordArray =
            elogAlignedAllocObjectArray<ELogRecordData>(ELOG_CACHE_LINE, ringBufferSize);
        if (m_recordArray == nullptr) {
            ELOG_REPORT_ERROR("Failed to allocate ring buffer of %" PRIu64
                              " elements for multi-quantum log target",
                              ringBufferSize);
            return false;
        }
    }

    if (m_bufferArray == nullptr) {
        m_bufferArray = elogAlignedAllocObjectArray<ELogBuffer>(ELOG_CACHE_LINE, ringBufferSize);
        if (m_bufferArray == nullptr) {
            ELOG_REPORT_ERROR("Failed to allocate log buffer array of %" PRIu64
                              " elements for quantum log target",
                              ringBufferSize);
            elogAlignedFreeObjectArray(m_bufferArray, ringBufferSize);
            return false;
        }
    }

    for (uint64_t i = 0; i < ringBufferSize; ++i) {
        m_recordArray[i].setLogBuffer(&m_bufferArray[i]);
    }

    m_ringBufferSize = ringBufferSize;
    return true;
}

void ELogMultiQuantumTarget::RingBuffer::terminate() {
    if (m_recordArray != nullptr) {
        elogAlignedFreeObjectArray(m_recordArray, m_ringBufferSize);
        m_recordArray = nullptr;
    }
    if (m_bufferArray != nullptr) {
        elogAlignedFreeObjectArray(m_bufferArray, m_ringBufferSize);
        m_bufferArray = nullptr;
    }
}

void ELogMultiQuantumTarget::RingBuffer::writeLogRecord(const ELogRecord& logRecord) {
    uint64_t writePos = m_writePos.fetch_add(1, std::memory_order_acquire);
    uint64_t readPos = m_readPos.load(std::memory_order_relaxed);

    // wait until there is no other writer contending for the same entry
    while (writePos - readPos >= m_ringBufferSize) {
        CPU_RELAX;
        readPos = m_readPos.load(std::memory_order_relaxed);
    }
    ELogRecordData& recordData = m_recordArray[writePos % m_ringBufferSize];
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
}

bool ELogMultiQuantumTarget::RingBuffer::readLogRecord(ELogRecord& logRecord,
                                                       ELogBuffer& logBuffer) {
    // get read/write pos
    uint64_t writePos = m_writePos.load(std::memory_order_relaxed);
    uint64_t readPos = m_readPos.load(std::memory_order_relaxed);

    // check if there is a new log record
    if (readPos == writePos) {
        return false;
    }

    // wait until record is ready for reading
    ELogRecordData& recordData = m_recordArray[readPos % m_ringBufferSize];
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

    // set record in reading state
    assert(entryState == ES_READY);
    if (!recordData.m_entryState.compare_exchange_strong(entryState, ES_READING,
                                                         std::memory_order_relaxed)) {
        assert(false);
    }

    // read record
    logRecord = recordData.m_logRecord;
    // TODO: check error
    logBuffer.assign(*recordData.m_logBuffer);

    // change state back to vacant and update read pos
    recordData.m_entryState.store(ES_VACANT, std::memory_order_relaxed);
    m_readPos.fetch_add(1, std::memory_order_relaxed);
    return true;
}

void ELogMultiQuantumTarget::RingBuffer::getReadWritePos(uint64_t& readPos, uint64_t& writePos) {
    readPos = m_readPos.load(std::memory_order_relaxed);
    writePos = m_writePos.load(std::memory_order_relaxed);
}

bool ELogMultiQuantumTarget::SortingFunnel::initialize(uint64_t ringBufferSize) {
    if (!m_ringBuffer.initialize(ringBufferSize)) {
        ELOG_REPORT_ERROR(
            "Failed to initialize ring buffer of sorting funnel in multi-quantum target");
        return false;
    }

    m_recordArray =
        elogAlignedAllocObjectArray<ELogRecordData*>(ELOG_CACHE_LINE, ringBufferSize, nullptr);
    if (m_recordArray == nullptr) {
        ELOG_REPORT_ERROR(
            "Failed to allocate record pointer array for sorting funnel in multi-quantum log "
            "target");
        m_ringBuffer.terminate();
        return false;
    }

    for (uint64_t i = 0; i < ringBufferSize; ++i) {
        m_recordArray[i] = &m_ringBuffer.m_recordArray[i];
    }

    m_ringBufferSize = ringBufferSize;
    return true;
}

void ELogMultiQuantumTarget::SortingFunnel::terminate() {
    if (m_recordArray != nullptr) {
        elogAlignedFreeObjectArray(m_recordArray, m_ringBufferSize);
        m_recordArray = nullptr;
    }
    m_ringBuffer.terminate();
}

void ELogMultiQuantumTarget::SortingFunnel::writeLogRecord(const ELogRecord& logRecord) {
    uint64_t writePos = m_writePos.fetch_add(1, std::memory_order_acquire);
    uint64_t readPos = m_readPos.load(std::memory_order_relaxed);

    // wait until there is no other writer contending for the same entry
    while (writePos - readPos >= m_ringBufferSize) {
        CPU_RELAX;
        readPos = m_readPos.load(std::memory_order_relaxed);
    }
    ELogRecordData* recordData = m_recordArray[writePos % m_ringBufferSize];
    EntryState entryState = recordData->m_entryState.load(std::memory_order_seq_cst);

    // now wait for entry to become vacant
    while (entryState != ES_VACANT) {
        CPU_RELAX;
        entryState = recordData->m_entryState.load(std::memory_order_relaxed);
    }
    assert(entryState == ES_VACANT);

    recordData->m_entryState.store(ES_WRITING, std::memory_order_seq_cst);
    // recordData.m_logRecord = logRecord;
    memcpy((void*)&recordData->m_logRecord, &logRecord, sizeof(ELogRecord));
    recordData->m_logBuffer->assign(logRecord.m_logMsg, logRecord.m_logMsgLen);
    recordData->m_logRecord.m_logMsg = recordData->m_logBuffer->getRef();
    recordData->m_entryState.store(ES_READY, std::memory_order_release);
}

bool ELogMultiQuantumTarget::SortingFunnel::readLogRecord(ELogRecord& logRecord,
                                                          ELogBuffer& logBuffer) {
    // get read/write pos
    uint64_t writePos = m_writePos.load(std::memory_order_relaxed);
    uint64_t readPos = m_readPos.load(std::memory_order_relaxed);

    // check if there is a new log record
    if (readPos == writePos) {
        return false;
    }

    // wait until record is ready for reading
    ELogRecordData* recordData = m_recordArray[readPos % m_ringBufferSize];
    EntryState entryState = recordData->m_entryState.load(std::memory_order_relaxed);
    // uint32_t localSpinCount = SPIN_COUNT_INIT;
    while (entryState != ES_READY) {
        // cpu relax then try again
        // NOTE: this degrades performance, not clear yet why
        // spin and exponential backoff
        // for (uint32_t spin = 0; spin < localSpinCount; ++spin) {
        //    CPU_RELAX;
        //}
        // localSpinCount *= 2;
        entryState = recordData->m_entryState.load(std::memory_order_relaxed);
        // we don't spin/back-off here since the state change is expected to happen
        // immediately
    }

    // set record in reading state
    assert(entryState == ES_READY);
    if (!recordData->m_entryState.compare_exchange_strong(entryState, ES_READING,
                                                          std::memory_order_relaxed)) {
        assert(false);
    }

    // read record
    logRecord = recordData->m_logRecord;
    // TODO: check error
    logBuffer.assign(*recordData->m_logBuffer);

    // change state back to vacant and update read pos
    recordData->m_entryState.store(ES_VACANT, std::memory_order_relaxed);
    m_readPos.fetch_add(1, std::memory_order_relaxed);

    // TODO: refactor this code (common with ring buffer) when done
    // TODO: refactor while class with quantum target when done
    return true;
}

}  // namespace elog
