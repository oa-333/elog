#ifndef __ELOG_MSG_TARGET_H__
#define __ELOG_MSG_TARGET_H__

#ifdef ELOG_ENABLE_MSG

#include <msg/msg.h>
#include <msg/msg_config.h>
#include <msg/msg_def.h>
#include <msg/msg_sender.h>
#include <transport/data_client.h>

#include "elog_target.h"
#include "msg/elog_binary_format_provider.h"
#include "msg/elog_msg.h"
#include "msg/elog_msg_config.h"
#include "msg/elog_msg_stats.h"

namespace elog {

/** @brief Abstract parent class for messaging log targets. */
class ELOG_API ELogMsgTarget : public ELogTarget,
                               public commutil::MsgFrameListener,
                               public commutil::MsgStatListener {
public:
    ELogMsgTarget(const char* name, const ELogMsgConfig& msgConfig,
                  commutil::DataClient* dataClient)
        : ELogTarget(name),
          m_msgConfig(msgConfig.m_commConfig),
          m_dataClient(dataClient),
          m_binaryFormatProvider(msgConfig.m_binaryFormatProvider),
          m_msgStats(nullptr),
          m_syncMode(msgConfig.m_syncMode),
          m_compress(msgConfig.m_compress),
          m_maxConcurrentRequests(msgConfig.m_maxConcurrentRequests) {}
    ELogMsgTarget(const ELogMsgTarget&) = delete;
    ELogMsgTarget(ELogMsgTarget&&) = delete;
    ELogMsgTarget& operator=(const ELogMsgTarget&) = delete;

    ELOG_DECLARE_LOG_TARGET(ELogMsgTarget)

    /**
     * @brief Notifies on sent message statistics.
     * @param msgSizeBytes The payload size (not including framing protocol header).
     * @param compressedMsgSizeBytes The compressed pay load size, in case compression is used,
     * otherwise zero.
     * @param status Denotes send result status. Zero means success.
     */
    void onSendMsgStats(uint32_t msgSizeBytes, uint32_t compressedMsgSizeBytes,
                        int status) override;

    /**
     * @brief Notifies on received message statistics.
     * @param msgSizeBytes The payload size (not including framing protocol header).
     * @param compressedMsgSizeBytes The compressed pay load size, in case compression is used,
     * otherwise zero.
     */
    void onRecvMsgStats(uint32_t msgSizeBytes, uint32_t compressedMsgSizeBytes) override;

    /**
     * @brief Handles incoming message buffer. Subclasses are responsible for actual
     * deserialization. The messaging layer provides only framing services.
     * @param connectionDetails The client's connection details.
     * @param msgHeader The header of the incoming meta-message frame.
     * @param msgBuffer The message buffer.
     * @param bufferSize The buffer length.
     * @param lastInBatch Designates whether this is the last message within a message batch. In
     * case of a single message this is always true.
     * @param batchSize The number of messages in the message batch. In case of a single message
     * this is always 1.
     * @return Message handling result. If message handling within a batch should continue then
     * return @ref E_OK, otherwise deriving sub-classes should return error code (e.g. irrecoverable
     * deserialization error), in which case @ref handleMsgError() is NOT called, and if some error
     * status needs to be sent to the client, then deriving sub-classes are responsible for that.
     */
    commutil::ErrorCode handleMsg(const commutil::ConnectionDetails& connectionDetails,
                                  const commutil::MsgHeader& msgHeader, const char* msgBuffer,
                                  uint32_t bufferSize, bool lastInBatch,
                                  uint32_t batchSize) override;

    /**
     * @brief Handle errors during message unpacking.
     * @param connectionDetails The client's connection details.
     * @param msgHeader The header of the incoming meta-message.
     * @param status Deserialization error status.
     */
    void handleMsgError(const commutil::ConnectionDetails& connectionDetails,
                        const commutil::MsgHeader& msgHeader, int status) override;

    /**
     * @brief Retrieves the number of messages that were fully processed by the log target. This
     * includes failed log messages. In case of a compound log target, the request is delegated to
     * the end log target.
     * @return The number of log messages that were fully processed or @ref ELOG_INVALID_MSG_COUNT
     * in case statistics collection for the log target is disabled.
     */
    uint64_t getProcessedMsgCount() override;

protected:
    /**
     * @brief Order the log target to start. In the case of a network target this means starting all
     * the machinery going AND performing full handshake to resolve binary format and protocol
     * version. So this is normally a blocking call, unless the user specifies in the constructor or
     * configuration to use asynchronous connect mode in which case the log target will accomplish
     * the connect/handshake in the background. The user can call @ref isReady() or @ref waitReady()
     * in order to verify.
     */
    bool startLogTarget() override;

    /** @brief Order the log target to stop (required for threaded targets). */
    bool stopLogTarget() override;

    /**
     * @brief Order the log target to write a log record (thread-safe).
     * @param logRecord The log record to write to the log target.
     * @param bytesWritten The number of bytes written to log.
     * @return The operation's result.
     */
    bool writeLogRecord(const ELogRecord& logRecord, uint64_t& bytesWritten) override;

    /** @brief Order the log target to flush. */
    bool flushLogTarget() override;

    /** @brief Creates a statistics object. */
    ELogStats* createStats() final;

    commutil::MsgConfig m_msgConfig;
    commutil::DataClient* m_dataClient;
    commutil::MsgClient m_msgClient;
    commutil::MsgSender m_msgSender;
    ELogBinaryFormatProvider* m_binaryFormatProvider;
    ELogMsgStats* m_msgStats;
    commutil::MsgBufferArray m_msgBufferArray;
    bool m_syncMode;
    bool m_compress;
    uint32_t m_maxConcurrentRequests;
};

}  // namespace elog

#endif  // ELOG_ENABLE_MSG

#endif  // __ELOG_MSG_TARGET_H__