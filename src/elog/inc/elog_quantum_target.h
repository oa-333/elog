#ifndef __ELOG_QUANTUM_TARGET_H__
#define __ELOG_QUANTUM_TARGET_H__

#include <atomic>
#include <new>
#include <thread>

#include "elog_def.h"
#include "elog_target.h"

namespace elog {

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
class ELOG_API ELogQuantumTarget : public ELogTarget {
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
     * @param congestionPolicy Specifies how to handle "no space for log record" condition.
     */
    ELogQuantumTarget(ELogTarget* logTarget, uint32_t bufferSize,
                      CongestionPolicy congestionPolicy = CongestionPolicy::CP_WAIT);
    ~ELogQuantumTarget() final {}

    /** @brief Sends a log record to a log target. */
    void log(const ELogRecord& logRecord) final;

    /** @brief Orders a buffered log target to flush it log messages. */
    void flush() final;

    /** @brief Queries whether the log target has written all pending messages. */
    bool isCaughtUp(uint64_t& writeCount, uint64_t& readCount) final {
        writeCount = m_writePos.load(std::memory_order_relaxed);
        readCount = m_readPos.load(std::memory_order_relaxed);
        return writeCount == readCount;
    }

    /** @brief As log target may be chained as in a list, this retrieves the final log target. */
    ELogTarget* getEndLogTarget() override { return m_logTarget; }

private:
    /** @brief Order the log target to start (required for threaded targets). */
    bool startLogTarget() final;

    /** @brief Order the log target to stop (required for threaded targets). */
    bool stopLogTarget() final;

private:
    enum EntryState : uint32_t { ES_VACANT, ES_WRITING, ES_READY, ES_READING };
    struct ELogRecordData {
        ELogRecord m_logRecord;
        std::string m_logMsg;
        std::atomic<EntryState> m_entryState;

        ELogRecordData() : m_entryState(ES_VACANT) {}
        ELogRecordData(const ELogRecordData& recordData)
            : m_logRecord(recordData.m_logRecord),
              m_logMsg(recordData.m_logMsg),
              m_entryState(recordData.m_entryState.load(std::memory_order_relaxed)) {}
    };

    typedef std::vector<ELogRecordData> LogRingBuffer;
    LogRingBuffer m_ringBuffer;
    // we put the write/read pos each in its own cache line to avoid false sharing
    // alignas(std::hardware_destructive_interference_size) std::atomic<uint64_t> m_writePos;
    // alignas(std::hardware_destructive_interference_size) std::atomic<uint64_t> m_readPos;
    std::atomic<uint64_t> m_writePos;
    std::atomic<uint64_t> m_readPos;
    CongestionPolicy m_congestionPolicy;

    ELogTarget* m_logTarget;
    std::thread m_logThread;
    bool m_stop;

    void logThread();
};

}  // namespace elog

#endif  // __ELOG_QUANTUM_TARGET_H__