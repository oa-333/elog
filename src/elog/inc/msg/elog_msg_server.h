#ifndef __ELOG_MSG_SERVER_H__
#define __ELOG_MSG_SERVER_H__

#ifdef ELOG_ENABLE_MSG

#include "elog_def.h"
#include "elog_rolling_bitset.h"
#include "msg/msg_server.h"

// TODO: this is temporary until a decision is made - currently protobuf is the default binary
// format for the elog protocol (unless an internal protocol is set up, which is currently unlikely)
#include "elog.pb.h"

/**
 * @def The default value used for specifying how much space the message server should reserve for
 * detecting duplicate messages (due to occasional resend by a client). The following formula should
 * be used to fine tune this value:
 *
 *      LatestMessageId - DelayedMessageId <= MessageDelaySpan
 *
 * In other words, by default the server can suffer a message being delayed until a message arrives
 * with an id that is greater than the delayed message id by 4096.
 */
#define ELOG_MSG_DEFAULT_MAX_DELAY_SPAN 4096

namespace elog {

/**
 * @brief Abstract class for implementing server-side of ELog log record reporting protocol.
 * Sub-classes should implement the message handling pure virtual method @ref handleLogRecordMsg().
 */
class ELOG_API ELogMsgServer : public commutil::MsgServer {
public:
    /**
     * @brief Construct a new ELogMsgServer object.
     *
     * @param name The server's name (for logging purposes).
     * @param byteOrder The byte order used to communicate with clients.
     * @param maxDelayMsgSpan The maximum message delay span per-client (see @ref
     * ELOG_MSG_DEFAULT_MAX_DELAY_SPAN for more details).
     */
    ELogMsgServer(const char* name, commutil::ByteOrder byteOrder,
                  uint32_t maxDelayMsgSpan = ELOG_MSG_DEFAULT_MAX_DELAY_SPAN)
        : m_name(name), m_byteOrder(byteOrder), m_maxDelayMsgSpan(maxDelayMsgSpan) {}
    ELogMsgServer(const ELogMsgServer&) = delete;
    ELogMsgServer(ELogMsgServer&&) = delete;
    ELogMsgServer& operator=(const ELogMsgServer&) = delete;
    ~ELogMsgServer() override {}

protected:
    /**
     * @brief Handles incoming message buffer provided by the framing protocol.
     * @param connectionDetails The client's connection details.
     * @param msgHeader The header of the incoming meta-message.
     * @param msgBuffer The message buffer. Valid only if status is zero.
     * @param bufferSize The buffer length. Valid only if status is zero.
     * @param lastInBatch Designates whether this is the last message within a message batch. In
     * case of a single message this is always true.
     * @param batchSize The number of messages in the message batch. In case of a single message
     * this is always 1.
     * @return Message handling result. If message handling within a batch should continue then
     * return @ref E_OK, otherwise deriving sub-classes should return error code (e.g. irrecoverable
     * deserialization error), in which case @ref handleMsgError() is NOT called, and if some error
     * status needs to be sent to the client, then deriving sub-classes are responsible for that.
     */
    commutil::ErrorCode handleMsg(const commutil::ConnectionDetails& connDetails,
                                  const commutil::MsgHeader& msgHeader, const char* buffer,
                                  uint32_t length, bool lastInBatch, uint32_t batchSize) override;

    /**
     * @brief Handle errors during message unpacking.
     * @param connectionDetails The client's connection details.
     * @param msgHeader The header of the incoming meta-message.
     * @param status Deserialization error status.
     */
    void handleMsgError(const commutil::ConnectionDetails& connDetails,
                        const commutil::MsgHeader& msgHeader, int status) override;

protected:
    /**
     * @brief Helper method for sending status back to a logging process.
     * @param connectionDetails The client's connection details.
     * @param msgHeader The header of the incoming meta-message.
     * @param status The response status (non-zero denotes an error).
     * @param recordsProcessed The number of processed messages this status message acknowledges
     * (not the total). After each batch (or occasionally), the server can report how many log
     * records it has processed since the previous status message report.
     */
    void sendStatus(const commutil::ConnectionDetails& connectionDetails,
                    const commutil::MsgHeader& msgHeader, int status, uint64_t recordsProcessed);

    /**
     * @brief Handle an incoming log record. The return code will be used as the status code in the
     * reply to the logging process (zero denotes success).
     */
    virtual int handleLogRecordMsg(elog_grpc::ELogRecordMsg* logRecordMsg) = 0;

protected:
    struct ELogSession : public commutil::MsgServer::Session {
        ELogRollingBitset m_rollingBitset;
        int m_status;

        ELogSession() {}
        ELogSession(uint64_t sessionId, const commutil::ConnectionDetails& connectionDetails,
                    uint32_t maxDelayMsgSpan)
            : commutil::MsgServer::Session(sessionId, connectionDetails),
              m_rollingBitset(ELogRollingBitset::computeWordCount(maxDelayMsgSpan)),
              m_status(0) {}

        ELogSession(const ELogSession&) = delete;
        ELogSession(ELogSession&&) = delete;
        ELogSession& operator=(const ELogSession&) = delete;
        ~ELogSession() override {}
    };

    /**
     * @brief Override this factory method to create custom session object with additional fields.
     * @param sessionId The session's unique id.
     * @param connectionDetails The incoming connection details.
     * @return Session* The resulting session object or null if failed or request denied.
     */
    commutil::MsgServer::Session* createSession(
        uint64_t sessionId, const commutil::ConnectionDetails& connectionDetails) override;

private:
    std::string m_name;
    commutil::ByteOrder m_byteOrder;
    uint32_t m_maxDelayMsgSpan;
};

}  // namespace elog

#endif  // ELOG_ENABLE_MSG

#endif  // __MSG_SERVER_H__