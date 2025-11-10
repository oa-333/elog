#ifndef __ELOG_MULTI_QUANTUM_TARGET_H__
#define __ELOG_MULTI_QUANTUM_TARGET_H__

#include <atomic>
#include <new>
#include <thread>

#include "elog_async_target.h"
#include "elog_def.h"

namespace elog {

/** @def The default number of readers used by the multi-quantum log target. */
#define ELOG_MQT_DEFAULT_READER_COUNT 1

/**
 * @def The default value for the number of iterations that each reader must iterate before a full
 * revisit of all ring buffers of all active threads takes place. By default each reader reads log
 * records only from active ring buffers.
 */
#define ELOG_MQT_DEFAULT_ACTIVE_REVISIT_COUNT 64

/**
 * @def The default value for the number of iterations that each reader must iterate before a full
 * revisit of all ring buffers of all threads takes place, whether active or non-active. By default
 * each reader reads log records only from active ring buffers or active threads.
 */
#define ELOG_MQT_DEFAULT_FULL_REVISIT_COUNT 256

/**
 * @def The default value for the maximum number of messages that can be read from a single thread
 * slot in a single strike. This limit exists to avoid starving other ring buffers, and
 * inadvertently also affect the sorting window size. The higher this value is, reading log messages
 * from the same ring buffer will be more cache-friendly, but in the same time will increase the
 * sorting window size, since the gap between the smallest and the largest timestamp is getting
 * bigger if we read more messages from one source.
 */
#define ELOG_MQT_DEFAULT_MAX_BATCH_SIZE 16

/**
 * @def By default we use 50 milliseconds sleep time between consecutive attempts to read from the
 * ring buffer, after it got empty. This allows the queue to "breath", before entering another phase
 * of fighting between writers and reader over read/write pos. If log target access is sporadic,
 * this default is good enough to collect ring buffer items before it gets full. If log target
 * access is rather frequent, consider lowering this period. If log flooding scenarios (short
 * bursts, not steady state), it is best to use a very large ring buffer size, and collect period of
 * zero. Performance will degrade a bit (less than 10%), but the ring buffer will not get full. The
 * downside is that one CPU core will be 100% busy.
 */
#define ELOG_MQT_DEFAULT_COLLECT_PERIOD_MICROS 50000

/**
 * @brief The quantum logger was designed to solve log flooding use case scenario that is
 * usually required when trying to pinpoint some very elusive bugs. In these situations,
 * enabling many log messages causes log flooding, and the incurred overhead may result in the
 * bug not being reproduced, due to varying timing. For this purpose a special log target was
 * devised, to minimize logging latency (such that timing would be almost not affected), while
 * enable logging large volumes of messages. In simpler words, it is an attempt to enable
 * observing a system without affecting the system behavior (at least not to an extent that
 * would be useless), hence the log target name. In order to achieve this a large lock-free ring
 * buffer is used as a temporary log message buffer. It should be noted that the trade-off here
 * is that there is a designated logging thread that takes 100% of a CPU core, just for logging.
 * Also, the ring buffer is limited in size, and if the log target cannot keep up with the pace,
 * log messages will be dropped. In case of log flooding, it is advise to couple this log target
 * with segmented log target. For extreme cases, consider logging to several log files, which
 * can be analyzed and reordered offline. This can be done by coupling this log target with a
 * combined log target that is in turn connected to several segmented log targets.
 */
class ELOG_API ELogMultiQuantumTarget : public ELogAsyncTarget {
public:
    /** @brief Quantum Target congestion policy constants. */
    enum class CongestionPolicy {
        /** @brief Designates to wait until there is room to post a message to the log target. */
        CP_WAIT,

        /**
         * @brief Designates to discard log messages if there is no room in the log target, not
         * including flush commands.
         */
        CP_DISCARD_LOG,

        /**
         * @brief Designates to discard log messages if there is no room in the log target,
         * including flush commands. This does not include the final poison message to stop the
         * quantum logging thread.
         */
        CP_DISCARD_ALL
    };

    /**
     * @brief Construct a new quantum log target object.
     * @param logTarget The receiving log target on the other end.
     * @param ringBufferSize The ring buffer size used by each thread. The maximum number of threads
     * is taken from initialization parameters of ELog (@see elog::initialize()).
     * @param readerCount The number of concurrent readers pulling log messages from thread ring
     * buffers and passing them to the sorting funnel.
     * @param activeRevisitPeriod The number of iterations that each reader thread makes after which
     * a full revisit of all active threads takes place. Normally only active ring buffers are read.
     * @param fullRevisitPeriod The number of iterations that each reader thread makes after which a
     * full revisit of all threads takes place, whether active or not. Normally only active ring
     * buffers are read.
     * @param collectPeriodMicros The time to wait between consecutive attempts to read from the
     * ring buffer. Zero means a tight loop, no CUP yield (yet).
     * @param congestionPolicy Specifies how to handle "no space for log record" condition.
     */
    ELogMultiQuantumTarget(ELogTarget* logTarget, uint32_t ringBufferSize,
                           uint32_t readerCount = ELOG_MQT_DEFAULT_READER_COUNT,
                           uint32_t activeRevisitPeriod = ELOG_MQT_DEFAULT_ACTIVE_REVISIT_COUNT,
                           uint32_t fullRevisitPeriod = ELOG_MQT_DEFAULT_FULL_REVISIT_COUNT,
                           uint32_t maxBatchSize = ELOG_MQT_DEFAULT_MAX_BATCH_SIZE,
                           uint64_t collectPeriodMicros = ELOG_MQT_DEFAULT_COLLECT_PERIOD_MICROS,
                           CongestionPolicy congestionPolicy = CongestionPolicy::CP_WAIT);
    ELogMultiQuantumTarget(const ELogMultiQuantumTarget&) = delete;
    ELogMultiQuantumTarget(ELogMultiQuantumTarget&&) = delete;
    ELogMultiQuantumTarget& operator=(const ELogMultiQuantumTarget&) = delete;

    ELOG_DECLARE_LOG_TARGET(ELogMultiQuantumTarget)

private:
    /** @brief Order the log target to start (required for threaded targets). */
    bool startLogTarget() final;

    /** @brief Order the log target to stop (required for threaded targets). */
    bool stopLogTarget() final;

    /**
     * @brief Order the log target to write a log record (thread-safe).
     * @param logRecord The log record to write to the log target.
     * @param bytesWritten The number of bytes written to log.
     * @return The operation's result.
     */
    bool writeLogRecord(const ELogRecord& logRecord, uint64_t& bytesWritten) final;

    /** @brief Orders a buffered log target to flush it log messages. */
    bool flushLogTarget() final;

private:
    enum EntryState : uint64_t { ES_VACANT, ES_WRITING, ES_READY, ES_READING };
    struct ELogRecordData {
        // NOTE: all members are well aligned
        ELogRecord m_logRecord;
        // TODO: we can use buffer allocated on heap once which can be passed directly to the ring
        // buffer of the sorting funnel
        ELogBuffer* m_logBuffer;
        std::atomic<EntryState> m_entryState;
        uint64_t m_padding[6];
        // NOTE: each record data takes 2 cache lines

        ELogRecordData() : m_logBuffer(nullptr), m_entryState(ES_VACANT) {}
        ELogRecordData(const ELogRecordData&) = delete;
        ELogRecordData(ELogRecordData&&) = delete;
        ELogRecordData& operator=(const ELogRecordData&) = delete;
        ~ELogRecordData() {}

        inline void setLogBuffer(ELogBuffer* logBuffer) { m_logBuffer = logBuffer; }
    };

    // a ring buffer used by each thread or as a the sorting funnel
    struct RingBuffer {
        std::atomic<uint64_t> m_isUsed;
        ELogRecordData* m_recordArray;
        ELogBuffer* m_bufferArray;
        uint64_t m_ringBufferSize;
        std::atomic<uint64_t> m_writePos;
        std::atomic<uint64_t> m_readPos;

        RingBuffer()
            : m_isUsed(0),
              m_recordArray(nullptr),
              m_bufferArray(nullptr),
              m_writePos(0),
              m_readPos(0) {}
        RingBuffer(const RingBuffer&) = delete;
        RingBuffer(RingBuffer&&) = delete;
        RingBuffer& operator=(const RingBuffer&) = delete;
        ~RingBuffer() {}

        bool initialize(uint64_t ringBufferSize);
        void terminate();
        void writeLogRecord(const ELogRecord& logRecord);
        bool readLogRecord(ELogRecord& logRecord, ELogBuffer& logBuffer);
        void getReadWritePos(uint64_t& readPos, uint64_t& writePos);
    };

    struct SortingFunnel {
        RingBuffer m_ringBuffer;
        ELogRecordData** m_recordArray;
        uint64_t m_ringBufferSize;
        std::atomic<uint64_t> m_writePos;
        std::atomic<uint64_t> m_readPos;

        SortingFunnel() : m_recordArray(nullptr), m_writePos(0), m_readPos(0) {}
        SortingFunnel(const SortingFunnel&) = delete;
        SortingFunnel(SortingFunnel&&) = delete;
        SortingFunnel& operator=(const SortingFunnel&) = delete;
        ~SortingFunnel() {}

        bool initialize(uint64_t ringBufferSize);
        void terminate();
        void writeLogRecord(const ELogRecord& logRecord);
        bool readLogRecord(ELogRecord& logRecord, ELogBuffer& logBuffer);
    };

    // sorting funnel iterator (required for sorting)
    class SortingFunnelIterator {
    public:
        using iterator_category = std::random_access_iterator_tag;
        using value_type = ELogRecordData*;
        using difference_type = std::ptrdiff_t;
        using pointer = ELogRecordData**;
        using reference = ELogRecordData*&;

        SortingFunnelIterator() : m_sortingFunnel(nullptr), m_sortingFunnelSize(0), m_pos(0) {}
        SortingFunnelIterator(const SortingFunnelIterator& itr)
            : m_sortingFunnel(itr.m_sortingFunnel),
              m_sortingFunnelSize(itr.m_sortingFunnelSize),
              m_pos(itr.m_pos) {}
        SortingFunnelIterator& operator=(const SortingFunnelIterator& itr) {
            m_sortingFunnel = itr.m_sortingFunnel;
            m_sortingFunnelSize = itr.m_sortingFunnelSize;
            m_pos = itr.m_pos;
            return *this;
        }
        SortingFunnelIterator(SortingFunnel* sortingFunnel, uint64_t ringBufferSize, uint64_t pos)
            : m_sortingFunnel(sortingFunnel), m_sortingFunnelSize(ringBufferSize), m_pos(pos) {}
        ~SortingFunnelIterator() {}

        reference operator*() {
            return m_sortingFunnel->m_recordArray[m_pos % m_sortingFunnelSize];
        }
        pointer operator->() {
            return &m_sortingFunnel->m_recordArray[m_pos % m_sortingFunnelSize];
        }
        reference operator*() const {
            return m_sortingFunnel->m_recordArray[m_pos % m_sortingFunnelSize];
        }
        pointer operator->() const {
            return &m_sortingFunnel->m_recordArray[m_pos % m_sortingFunnelSize];
        }
        SortingFunnelIterator& operator++() {
            ++m_pos;
            return *this;
        }
        SortingFunnelIterator operator++(int) {
            SortingFunnelIterator temp = *this;
            ++m_pos;
            return temp;
        }
        SortingFunnelIterator& operator--() {
            --m_pos;
            return *this;
        }
        SortingFunnelIterator operator--(int) {
            SortingFunnelIterator temp = *this;
            --m_pos;
            return temp;
        }
        bool operator==(const SortingFunnelIterator& other) const { return m_pos == other.m_pos; }
        bool operator!=(const SortingFunnelIterator& other) const { return m_pos != other.m_pos; }
        bool operator<(const SortingFunnelIterator& other) const { return m_pos < other.m_pos; }
        bool operator<=(const SortingFunnelIterator& other) const { return m_pos <= other.m_pos; }
        bool operator>(const SortingFunnelIterator& other) const { return m_pos > other.m_pos; }
        bool operator>=(const SortingFunnelIterator& other) const { return m_pos >= other.m_pos; }
        SortingFunnelIterator operator+(std::ptrdiff_t distance) const {
            return SortingFunnelIterator(m_sortingFunnel, m_sortingFunnelSize,
                                         addDistance(m_pos, distance));
        }
        SortingFunnelIterator operator-(std::ptrdiff_t distance) const {
            return SortingFunnelIterator(m_sortingFunnel, m_sortingFunnelSize,
                                         subDistance(m_pos, distance));
        }
        SortingFunnelIterator& operator+=(std::ptrdiff_t distance) {
            m_pos = addDistance(m_pos, distance);
            return *this;
        }
        SortingFunnelIterator& operator-=(std::ptrdiff_t distance) {
            m_pos = subDistance(m_pos, distance);
            return *this;
        }
        reference operator[](std::ptrdiff_t distance) {
            return m_sortingFunnel
                ->m_recordArray[addDistance(m_pos, distance) % m_sortingFunnelSize];
        }
        reference operator[](std::ptrdiff_t distance) const {
            return m_sortingFunnel
                ->m_recordArray[addDistance(m_pos, distance) % m_sortingFunnelSize];
        }
        std::ptrdiff_t operator-(SortingFunnelIterator rhs) const {
            return ((std::ptrdiff_t)m_pos) - ((std::ptrdiff_t)rhs.m_pos);
        }

    private:
        SortingFunnel* m_sortingFunnel;
        uint64_t m_sortingFunnelSize;
        uint64_t m_pos;

        static uint64_t addDistance(uint64_t pos, std::ptrdiff_t distance) {
            if (distance >= 0) {
                assert(UINT64_MAX - (uint64_t)distance > pos);
                return pos + (uint64_t)distance;
            } else {
                assert(pos >= (uint64_t)-distance);
                return pos - (uint64_t)-distance;
            }
        }

        static uint64_t subDistance(uint64_t pos, std::ptrdiff_t distance) {
            return addDistance(pos, -distance);
        }
    };

    ELOG_CACHE_ALIGN RingBuffer* m_ringBuffers;
    ELOG_CACHE_ALIGN std::atomic<uint64_t>* m_activeThreads;
    ELOG_CACHE_ALIGN std::atomic<uint64_t>* m_activeRingBuffers;
    ELOG_CACHE_ALIGN std::atomic<uint64_t>* m_threadLogTime;
    ELOG_CACHE_ALIGN uint64_t* m_recentThreadLogTime;
    ELOG_CACHE_ALIGN SortingFunnel m_sortingFunnel;

    uint64_t m_maxThreadCount;
    uint64_t m_bitsetSize;
    uint64_t m_ringBufferSize;
    uint64_t m_readerCount;
    uint64_t m_activeRevisitPeriod;
    uint64_t m_fullRevisitPeriod;
    uint64_t m_maxBatchSize;
    uint64_t m_collectPeriodMicros;
    uint64_t m_sortingFunnelSize;
    // CongestionPolicy m_congestionPolicy;

    std::vector<std::thread> m_readerThreads;
    std::thread m_sortingThread;

    std::atomic<uint64_t> m_readCount;
    std::atomic<uint64_t> m_funnelCount;
    std::atomic<uint64_t> m_stableCount;
    std::atomic<uint64_t> m_sortCount;
    std::atomic<uint64_t> m_shipCount;

    void readerThread(uint64_t readerId, uint64_t fromWordIndex, uint64_t toWordIndex);

    bool visitActiveRingBuffers(uint64_t wordIndex);

    bool revisitAllActiveThreads(uint64_t wordIndex);

    bool revisitAllThreads(uint64_t wordIndex);

    bool readThreadRingBuffer(uint64_t slotId);

    // move from ring buffer to funnel, return true if poison encountered
    // the max timestamp is valid only if at least one new message was extracted from the ring
    // buffer
    bool extractToSortingFunnel(RingBuffer* ringBuffer, uint64_t& maxTimeStamp, bool& isValid,
                                bool& extractedAllRecords);

    void sortingThread();

    // get minimum timestamp among the maximum timestamp of all active threads, return if value is
    // valid (i.e. all active threads have reported at least once)
    bool getMinTimeStamp(uint64_t& maxTimeStamp);

    // wait for range to stabilize
    void waitFunnelRangeStable(uint64_t readPos, uint64_t endPos);

    // sort range
    void sortFunnel(uint64_t readPos, uint64_t endPos);

    // sort helper
    static bool isRecordDataLess(const ELogRecordData* lhs, const ELogRecordData* rhs);

    // ship ready records from sorted funnel to destination log target, return true if poison seen
    bool shipReadySortedRecords(uint64_t readPos, uint64_t endPos, uint64_t maxTimeStamp);

    uint64_t getThreadSlotId();
    uint64_t obtainThreadSlot();
    void releaseThreadSlot(uint64_t slotId);
    inline bool isThreadActive(uint64_t slotId) {
        uint64_t index = slotId / 64;
        uint64_t offset = slotId % 64;
        return m_activeThreads[index].load(std::memory_order_relaxed) & (1ull << offset);
    }

    inline void raiseThreadBit(uint64_t slotId) { raiseBit(m_activeThreads, slotId); }
    inline void resetThreadBit(uint64_t slotId) { resetBit(m_activeThreads, slotId); }
    inline void raiseRingBufferBit(uint64_t slotId) { raiseBit(m_activeRingBuffers, slotId); }
    inline void resetRingBufferBit(uint64_t slotId) { resetBit(m_activeRingBuffers, slotId); }

    void raiseBit(std::atomic<uint64_t>* bitset, uint64_t slotId);
    void resetBit(std::atomic<uint64_t>* bitset, uint64_t slotId);

    // TLS cleanup callback
    static void cleanupThreadSlot(void* key);

    // free all allocated resources
    void cleanup();
};

}  // namespace elog

#endif  // __ELOG_MULTI_QUANTUM_TARGET_H__