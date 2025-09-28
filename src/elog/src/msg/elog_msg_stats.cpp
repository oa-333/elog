#include "msg/elog_msg_stats.h"

#include <cinttypes>

#include "elog_report.h"

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogMsgStats)

bool ELogMsgStats::initialize(uint32_t maxThreads) {
    if (!ELogStats::initialize(maxThreads)) {
        return false;
    }
    if (!m_sendCount.initialize(maxThreads) || !m_sendFailCount.initialize(maxThreads) ||
        !m_sendByteCount.initialize(maxThreads) ||
        !m_compressedSendByteCount.initialize(maxThreads) || !m_recvCount.initialize(maxThreads) ||
        !m_recvFailCount.initialize(maxThreads) || !m_recvByteCount.initialize(maxThreads) ||
        !m_processedMsgCount.initialize(maxThreads)) {
        ELOG_REPORT_ERROR("Failed to initialize message statistics variables");
        terminate();
        return false;
    }
    return true;
}

void ELogMsgStats::terminate() {
    ELogStats::terminate();
    m_sendCount.terminate();
    m_sendFailCount.terminate();
    m_sendByteCount.terminate();
    m_compressedSendByteCount.terminate();
    m_recvCount.terminate();
    m_recvFailCount.terminate();
    m_recvByteCount.terminate();
    m_processedMsgCount.terminate();
}

void ELogMsgStats::updateSendStats(uint64_t sendBytes, uint64_t compressedBytes, int status) {
    if (status != 0) {
        incrementSendFailCount();
    } else {
        incrementSendCount();
        addSendBytesCount(sendBytes);
        if (compressedBytes > 0) {
            addCompressedSendBytesCount(compressedBytes);
        }
    }
}

void ELogMsgStats::updateRecvStats(uint64_t recvBytes, uint64_t msgProcessed) {
    incrementRecvCount();
    addRecvByteCount(recvBytes);
    addProcessedMsgCount(msgProcessed);
}

void ELogMsgStats::toString(ELogBuffer& buffer, ELogTarget* logTarget, const char* msg /* = "" */) {
    ELogStats::toString(buffer, logTarget, msg);

    // send statistics
    uint64_t sendCount = m_sendCount.getSum();
    buffer.appendArgs("\tSend count: %" PRIu64 "\n", sendCount);
    buffer.appendArgs("\tSend fail count: %" PRIu64 "\n", m_sendFailCount.getSum());
    if (sendCount > 0) {
        uint64_t avgBufferBytes = m_sendByteCount.getSum() / sendCount;
        buffer.appendArgs("\tAverage send buffer size: %" PRIu64 " bytes\n", avgBufferBytes);
        uint64_t compressedBufferBytes = m_compressedSendByteCount.getSum();
        if (compressedBufferBytes > 0) {
            uint64_t avgCompressedBufferBytes = compressedBufferBytes / sendCount;
            buffer.appendArgs("\tAverage compressed send buffer size: %" PRIu64 " bytes\n",
                              avgCompressedBufferBytes);
            double compressionRate =
                (1.0 - ((double)avgCompressedBufferBytes) / ((double)avgBufferBytes)) * 100.0;
            buffer.appendArgs("\tCompression rate: %.2f%%\n", compressionRate);
        }
    } else {
        buffer.appendArgs("\tAverage buffer size: N/A\n");
    }

    // recv statistics
    uint64_t recvCount = m_recvCount.getSum();
    buffer.appendArgs("\tRecv count: %" PRIu64 "\n", recvCount);
    buffer.appendArgs("\tRecv fail count: %" PRIu64 "\n", m_recvFailCount.getSum());
    if (recvCount > 0) {
        uint64_t avgBufferBytes = m_recvByteCount.getSum() / recvCount;
        buffer.appendArgs("\tAverage recv buffer size: %" PRIu64 " bytes\n", avgBufferBytes);
        // receive end has no compression at the moment
    } else {
        buffer.appendArgs("\tAverage recv buffer size: N/A\n");
    }
    buffer.appendArgs("\tProcessed message count: %" PRIu64 "\n", m_processedMsgCount.getSum());
}

void ELogMsgStats::resetThreadCounters(uint64_t slotId) {
    ELogStats::resetThreadCounters(slotId);
    m_sendCount.reset(slotId);
    m_sendFailCount.reset(slotId);
    m_sendByteCount.reset(slotId);
    m_compressedSendByteCount.reset(slotId);
    m_recvCount.reset(slotId);
    m_recvFailCount.reset(slotId);
    m_recvByteCount.reset(slotId);
    m_processedMsgCount.reset(slotId);
}

}  // namespace elog