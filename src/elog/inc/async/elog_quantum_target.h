#ifndef __ELOG_QUANTUM_TARGET_H__
#define __ELOG_QUANTUM_TARGET_H__

#include <atomic>
#include <new>
#include <thread>

#include "elog_async_target.h"
#include "elog_def.h"

namespace elog {

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
#define ELOG_DEFAULT_COLLECT_PERIOD_MICROS 50000

/**
 * @brief The quantum logger was designed to solve log flooding use case scenario that is usually
 * required when trying to pinpoint some very elusive bugs. In these situations, enabling many log
 * messages causes log flooding, and the incurred overhead may result in the bug not being
 * reproduced, due to varying timing. For this purpose a special log target was devised, to minimize
 * logging latency (such that timing would be almost not affected), while enable logging large
 * volumes of messages. In simpler words, it is an attempt to enable observing a system without
 * affecting the system behavior (at least not to an extent that would be useless), hence the log
 * target name. In order to achieve this a large lock-free ring buffer is used as a temporary log
 * message buffer. It should be noted that the trade-off here is that there is a designated logging
 * thread that takes 100% of a CPU core, just for logging. Also, the ring buffer is limited in size,
 * and if the log target cannot keep up with the pace, log messages will be dropped. In case of log
 * flooding, it is advise to couple this log target with segmented log target. For extreme cases,
 * consider logging to several log files, which can be analyzed and reordered offline. This can be
 * done by coupling this log target with a combined log target that is in turn connected to several
 * segmented log targets.
 */
class ELOG_API ELogQuantumTarget : public ELogAsyncTarget {
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
     * @param bufferSize The ring buffer size used by the quantum log target.
     * @param collectPeriodMicros The time to wait between consecutive attempts to read from the
     * ring buffer. Zero means a tight loop, no CUP yield (yet).
     * @param congestionPolicy Specifies how to handle "no space for log record" condition.
     */
    ELogQuantumTarget(ELogTarget* logTarget, uint32_t bufferSize,
                      uint64_t collectPeriodMicros = ELOG_DEFAULT_COLLECT_PERIOD_MICROS,
                      CongestionPolicy congestionPolicy = CongestionPolicy::CP_WAIT);
    ELogQuantumTarget(const ELogQuantumTarget&) = delete;
    ELogQuantumTarget(ELogQuantumTarget&&) = delete;
    ELogQuantumTarget& operator=(const ELogQuantumTarget&) = delete;

    ELOG_DECLARE_LOG_TARGET(ELogQuantumTarget)

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

    ELOG_CACHE_ALIGN ELogRecordData* m_ringBuffer;
    ELogBuffer* m_bufferArray;
    uint64_t m_ringBufferSize;
    uint64_t m_collectPeriodMicros;

    // NOTE: write pos is usually very noisy, so we don't want it to affect read pos, which usually
    // is much slower, therefore, we put read pos in a separate cache line
    // in addition, ring buffer ptr, buffer array ptr and size are not changing, so they are good
    // cache candidates, so we would like to keep them unaffected as well, so we put write pos also
    // in it sown cache line
    ELOG_CACHE_ALIGN std::atomic<uint64_t> m_writePos;
    ELOG_CACHE_ALIGN std::atomic<uint64_t> m_readPos;
    // CongestionPolicy m_congestionPolicy;

    std::thread m_logThread;

    void logThread();
};

}  // namespace elog

#endif  // __ELOG_QUANTUM_TARGET_H__