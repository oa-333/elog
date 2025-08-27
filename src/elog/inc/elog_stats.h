#ifndef __ELOG_STATS_H__
#define __ELOG_STATS_H__

#include <atomic>

#include "elog_buffer.h"
#include "elog_def.h"

namespace elog {

/** @def Invalid statistics slot id. */
#define ELOG_INVALID_STAT_SLOT_ID ((uint64_t)-1)

class ELOG_API ELogTarget;
class ELOG_API ELogLogger;

/** @brief A single log counter. */
struct ELOG_API ELogCounter {
    /** @brief A volatile log counter (avoid caching, so reader sees correct values). */
    // TODO: check this further, is there another way to ensure reader sees correct value?
    volatile uint64_t m_counter;

    ELogCounter() : m_counter(0) {}
    ELogCounter(const ELogCounter&) = delete;
    ELogCounter(ELogCounter&&) = delete;
    ELogCounter& operator=(const ELogCounter&) = delete;
    ~ELogCounter() {}
};

/** @brief A single statistics variable. */
struct ELOG_API ELogStatVar {
    ELogStatVar() : m_threadCounters(nullptr), m_maxThreads(0) {}
    ELogStatVar(const ELogStatVar&) = delete;
    ELogStatVar(ELogStatVar&&) = delete;
    ELogStatVar& operator=(const ELogStatVar&) = delete;
    ~ELogStatVar() { terminate(); }

    /** @brief Initializes the statistics variable. */
    bool initialize(uint32_t maxThreads);

    /** @brief Terminates the statistics variable. */
    void terminate();

    /**
     * @brief Adds an amount to the statistics variable.
     * @param slotId The allocated slot for the current thread.
     * @param amount The amount to add to the counter.
     */
    inline void add(uint64_t slotId, uint64_t amount) {
        m_threadCounters[slotId].m_counter += amount;
    }

    /** @brief Resets the counter value for a specific thread. */
    inline void reset(uint64_t slotId) { m_threadCounters[slotId].m_counter = 0; }

    /**
     * @brief Adds the threads counters of another statistics variable.
     * @param statVar The statistics variable to add.
     */
    inline void addVar(const ELogStatVar& statVar) {
        for (uint32_t i = 0; i < m_maxThreads; ++i) {
            m_threadCounters[i].m_counter += statVar.m_threadCounters[i].m_counter;
        }
    }

    /** @brief Retrieves the sum of all thread counters. */
    inline uint64_t getSum() const {
        uint64_t sum = 0;
        for (uint32_t i = 0; i < m_maxThreads; ++i) {
            sum += m_threadCounters[i].m_counter;
        }
        return sum;
    }

private:
    ELogCounter* m_threadCounters;
    uint32_t m_maxThreads;
};

/** @brief Parent class for log target statistics. */
struct ELOG_API ELogStats {
    ELogStats() {}
    ELogStats(const ELogStats&) = delete;
    ELogStats(ELogStats&&) = delete;
    ELogStats& operator=(const ELogStats&) = delete;
    virtual ~ELogStats() {}

    /** @brief Initializes the statistics variable. */
    virtual bool initialize(uint32_t maxThreads);

    /** @brief Terminates the statistics variable. */
    virtual void terminate();

    // log message statistics
    inline void incrementMsgDiscarded() { m_msgDiscarded.add(getSlotId(), 1); }
    inline void incrementMsgSubmitted() { m_msgSubmitted.add(getSlotId(), 1); }
    inline void incrementMsgWritten() { m_msgWritten.add(getSlotId(), 1); }
    inline void incrementMsgFailWrite() { m_msgFailWrite.add(getSlotId(), 1); }

    // byte count statistics
    inline void addBytesSubmitted(uint64_t bytes) { m_bytesSubmitted.add(getSlotId(), bytes); }
    inline void addBytesWritten(uint64_t bytes) { m_bytesWritten.add(getSlotId(), bytes); }
    inline void addBytesFailWrite(uint64_t bytes) { m_bytesFailWrite.add(getSlotId(), bytes); }

    // flush statistics
    inline void incrementFlushSubmitted() { m_flushSubmitted.add(getSlotId(), 1); }
    inline void incrementFlushExecuted() { m_flushExecuted.add(getSlotId(), 1); }
    inline void incrementFlushFailed() { m_flushFailed.add(getSlotId(), 1); }
    inline void incrementFlushDiscarded() { m_flushDiscarded.add(getSlotId(), 1); }

    // log message statistics (user provides lot id)
    inline void incrementMsgDiscarded(uint64_t slotId) { m_msgDiscarded.add(slotId, 1); }
    inline void incrementMsgSubmitted(uint64_t slotId) { m_msgSubmitted.add(slotId, 1); }
    inline void incrementMsgWritten(uint64_t slotId) { m_msgWritten.add(slotId, 1); }
    inline void incrementMsgFailWrite(uint64_t slotId) { m_msgFailWrite.add(slotId, 1); }

    // byte count statistics (user provides lot id)
    inline void addBytesSubmitted(uint64_t slotId, uint64_t bytes) {
        m_bytesSubmitted.add(slotId, bytes);
    }
    inline void addBytesWritten(uint64_t slotId, uint64_t bytes) {
        m_bytesWritten.add(slotId, bytes);
    }
    inline void addBytesFailWrite(uint64_t slotId, uint64_t bytes) {
        m_bytesFailWrite.add(slotId, bytes);
    }

    // flush statistics (user provides lot id)
    inline void incrementFlushSubmitted(uint64_t slotId) { m_flushSubmitted.add(slotId, 1); }
    inline void incrementFlushExecuted(uint64_t slotId) { m_flushExecuted.add(slotId, 1); }
    inline void incrementFlushFailed(uint64_t slotId) { m_flushFailed.add(slotId, 1); }
    inline void incrementFlushDiscarded(uint64_t slotId) { m_flushDiscarded.add(slotId, 1); }

    /**
     * @brief Prints statistics to an output string buffer.
     * @param buffer The output string buffer.
     * @param logTarget The log target whose statistics are to be printed.
     * @param msg Any title message that would precede the report.
     * @note If overriding, first call perent class @ref toString(), then print your own stats.
     */
    virtual void toString(ELogBuffer& buffer, ELogTarget* logTarget, const char* msg = "");

    inline uint64_t getBytesWritten() const { return m_bytesWritten.getSum(); }
    inline uint64_t getMsgSubmitted() const { return m_msgSubmitted.getSum(); }
    inline uint64_t getMsgWritten() const { return m_msgWritten.getSum(); }
    inline uint64_t getMsgFailWrite() const { return m_msgFailWrite.getSum(); }

    /**
     * @brief Retrieves the slot id for the current thread. The slot id is used to access the
     * same counter in each statistics variable, which is dedicates to the calling thread.
     * @return The slot id or @ref ELOG_INVALID_STAT_SLOT_ID if there is no available slot.
     * @note The slot is allocated once per thread.
     */
    static uint64_t getSlotId();

    /** @brief Releases the statistics slot for the current thread. */
    virtual void resetThreadCounters(uint64_t slotId);

private:
    /** @brief Number of log messages discarded by the log target due to log level or filter. */
    ELogStatVar m_msgDiscarded;

    /**
     * @brief Number of log messages submitted to the log target for writing.
     * @note In the context of synchronous log targets, this counter is updated BEFORE write is
     * performed. In the context of asynchronous log targets, this counter is quite meaningless and
     * is updated each time before a log message is queued for later handling.
     */
    ELogStatVar m_msgSubmitted;

    /**
     * @brief Number of log messages successfully written to file/transport-layer.
     * @note In the context of synchronous log targets, this counter is updated AFTER write has
     * successfully executed. In the context of asynchronous log targets, this counter denotes the
     * number of log messages queued for logging, and is updated each time after a log message is
     * successfully queued for later handling.
     */
    ELogStatVar m_msgWritten;

    /**
     * @brief Number of failures to write log messages to file/transport-layer.
     * @note In the context of asynchronous log targets, this will always be zero (since passing a
     * queued log message to the inner log target always succeeds).
     */
    ELogStatVar m_msgFailWrite;

    /**
     * @brief Number of bytes submitted to the log target for writing.
     * @note In the context of synchronous log targets, this counter is updated BEFORE write is
     * performed. In the context of asynchronous log targets, this counter denotes the number of
     * bytes successfully queued for later handling. This number is expected to be smaller than
     * actual bytes being logged, since the log record at this staged has not been fully formatted.
     * In the case of binary cached format message, this number would be even smaller.
     */
    ELogStatVar m_bytesSubmitted;

    /**
     * @brief Number of bytes written to file/transport-layer (not including errors).
     * @note In the context of synchronous log targets, this counter is updated AFTER write has
     * successfully executed. In the context of asynchronous log targets, this denotes the total
     * number bytes passed to the inner log target (in the context of the asynchronous log thread).
     */
    ELogStatVar m_bytesWritten;

    /**
     * @brief Number of bytes in failures to write log messages to file/transport-layer.
     * @note In the context of asynchronous log targets, this will always be zero (since passing a
     * queued log message to the inner log target always succeeds).
     */
    ELogStatVar m_bytesFailWrite;

    /** @brief Number of flush requests submitted to the log target. */
    ELogStatVar m_flushSubmitted;

    /**
     * @brief Number of flush requests executed successfully.
     * @note In the context of asynchronous log targets, this denotes the number flush requests
     * passed to the inner log target (in the context of the asynchronous log thread).
     */
    ELogStatVar m_flushExecuted;

    /**
     * @brief Number of flush requests that failed to execute.
     * @note In the context of asynchronous log targets, this will always be zero (since passing a
     * queued flush request to the inner log target always succeeds).
     */
    ELogStatVar m_flushFailed;

    /**
     * @brief The number of flush requests discarded due to internal log target considerations. This
     * does not normally denote any error, but rather that executing the flush request is either
     * meaningless or redundant. One example is when log file segment is being closed (so no flush
     * is required). Another example is with group flush, where followers are not performing any
     * flush but rather wait for group leader to execute flush.
     * @note In case discarded flush requests are reported, then the flush executed counter includes
     * also the number of discarded flush requests (i.e. discarded flush requests are being regarded
     * as a successful flush request execution).
     */
    ELogStatVar m_flushDiscarded;
};

}  // namespace elog

#endif  // __ELOG_STATS_H__