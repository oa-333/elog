#ifndef __ELOG_MSG_STATS_H__
#define __ELOG_MSG_STATS_H__

#include "elog_stats.h"

namespace elog {

struct ELOG_API ELogMsgStats : public ELogStats {
    ELogMsgStats() {}
    ELogMsgStats(const ELogMsgStats&) = delete;
    ELogMsgStats(ELogMsgStats&&) = delete;
    ELogMsgStats& operator=(const ELogMsgStats&) = delete;
    ~ELogMsgStats() final {}

    /** @brief Initializes the statistics variable. */
    bool initialize(uint32_t maxThreads) override;

    /** @brief Terminates the statistics variable. */
    void terminate() override;

    // send statistics
    inline void incrementSendCount() { m_sendCount.add(getSlotId(), 1); }
    inline void incrementSendFailCount() { m_sendFailCount.add(getSlotId(), 1); }
    inline void addSendBytesCount(uint64_t bytes) { m_sendByteCount.add(getSlotId(), bytes); }
    inline void addCompressedSendBytesCount(uint64_t bytes) {
        m_compressedSendByteCount.add(getSlotId(), bytes);
    }

    void updateSendStats(uint64_t sendBytes, uint64_t compressedBytes, int status);
    void updateRecvStats(uint64_t recvBytes, uint64_t msgProcessed);

    // recv statistics
    inline void incrementRecvCount() { m_recvCount.add(getSlotId(), 1); }
    inline void incrementRecvFailCount() { m_sendFailCount.add(getSlotId(), 1); }
    inline void addRecvByteCount(uint64_t bytes) { m_recvByteCount.add(getSlotId(), bytes); }
    inline void addProcessedMsgCount(uint64_t msgCount) {
        m_processedMsgCount.add(getSlotId(), msgCount);
    }

    /**
     * @brief Prints log target statistics into a string buffer, adding the log buffer statistics.
     * @param buffer The output string buffer.
     * @param logTarget The log target whose statistics are to be printed.
     * @param msg Any title message that would precede the report.
     */
    void toString(ELogBuffer& buffer, ELogTarget* logTarget, const char* msg = "") override;

    inline const ELogStatVar& getSendCount() const { return m_sendCount; }
    inline const ELogStatVar& getSendFailCount() const { return m_sendFailCount; }
    inline const ELogStatVar& getSendByteCount() const { return m_sendByteCount; }
    inline const ELogStatVar& getCompressedSendByteCount() const {
        return m_compressedSendByteCount;
    }

    inline const ELogStatVar& getRecvCount() const { return m_recvCount; }
    inline const ELogStatVar& getRecvFailCount() const { return m_recvFailCount; }
    inline const ELogStatVar& getRecvByteCount() const { return m_recvByteCount; }
    inline const ELogStatVar& getProcessedMsgCount() const { return m_processedMsgCount; }

    /** @brief Releases the statistics slot for the current thread. */
    void resetThreadCounters(uint64_t slotId) override;

private:
    /** @brief The total number of times sending log data to the transport layer. */
    ELogStatVar m_sendCount;

    /** @brief The total number of times sending log data to the transport layer failed. */
    ELogStatVar m_sendFailCount;

    /** @brief The total number of bytes written to the transport layer. */
    ELogStatVar m_sendByteCount;

    /** @brief The total number of compressed bytes written to the transport layer. */
    ELogStatVar m_compressedSendByteCount;

    /** @brief The total number of times receiving status responses from the transport layer. */
    ELogStatVar m_recvCount;

    /** @brief The total number of times receiving log data from the transport layer failed. */
    ELogStatVar m_recvFailCount;

    /** @brief The total number of bytes received from the transport layer. */
    ELogStatVar m_recvByteCount;

    /** @brief The number of log messages processed and acknowledged by the server. */
    ELogStatVar m_processedMsgCount;
};

}  // namespace elog

#endif  // __ELOG_MSG_STATS_H__