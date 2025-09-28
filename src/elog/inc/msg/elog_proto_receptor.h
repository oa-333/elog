#ifndef __ELOG_PROTO_RECEPTOR_H__
#define __ELOG_PROTO_RECEPTOR_H__

#ifdef ELOG_ENABLE_MSG

#include "elog.pb.h"
#include "elog_def.h"
#include "elog_field_receptor.h"

namespace elog {

class ELogProtoReceptor : public ELogFieldReceptor {
public:
    ELogProtoReceptor()
        : ELogFieldReceptor(ELogFieldReceptor::ReceiveStyle::RS_BY_NAME), m_logRecordMsg(nullptr) {}
    ELogProtoReceptor(const ELogProtoReceptor&) = delete;
    ELogProtoReceptor(ELogProtoReceptor&&) = delete;
    ELogProtoReceptor& operator=(const ELogProtoReceptor&) = delete;
    ~ELogProtoReceptor() override {}

    /** @brief Provide from outside a log record message to be filled-in by the field receptor. */
    inline void setLogRecordMsg(elog_grpc::ELogRecordMsg* logRecordMsg) {
        m_logRecordMsg = logRecordMsg;
    }

    /** @brief Receives any static text found outside of log record field references. */
    void receiveStaticText(uint32_t typeId, const std::string& text,
                           const ELogFieldSpec& fieldSpec) override;

    /** @brief Receives the log record id. */
    void receiveRecordId(uint32_t typeId, uint64_t recordId,
                         const ELogFieldSpec& fieldSpec) override;

    /** @brief Receives the host name. */
    void receiveHostName(uint32_t typeId, const char* hostName,
                         const ELogFieldSpec& fieldSpec) override;

    /** @brief Receives the user name. */
    void receiveUserName(uint32_t typeId, const char* userName,
                         const ELogFieldSpec& fieldSpec) override;

    /** @brief Receives the program name. */
    void receiveProgramName(uint32_t typeId, const char* programName,
                            const ELogFieldSpec& fieldSpec) override;

    /** @brief Receives the process id. */
    void receiveProcessId(uint32_t typeId, uint64_t processId,
                          const ELogFieldSpec& fieldSpec) override;

    /** @brief Receives the thread id. */
    void receiveThreadId(uint32_t typeId, uint64_t threadId,
                         const ELogFieldSpec& fieldSpec) override;

    /** @brief Receives the thread name. */
    void receiveThreadName(uint32_t typeId, const char* threadName,
                           const ELogFieldSpec& fieldSpec) override;

    /** @brief Receives the log source name. */
    void receiveLogSourceName(uint32_t typeId, const char* logSourceName,
                              const ELogFieldSpec& fieldSpec) override;

    /** @brief Receives the module name. */
    void receiveModuleName(uint32_t typeId, const char* moduleName,
                           const ELogFieldSpec& fieldSpec) override;

    /** @brief Receives the file name. */
    void receiveFileName(uint32_t typeId, const char* fileName,
                         const ELogFieldSpec& fieldSpec) override;

    /** @brief Receives the logging line. */
    void receiveLineNumber(uint32_t typeId, uint64_t lineNumber,
                           const ELogFieldSpec& fieldSpec) override;

    /** @brief Receives the function name. */
    void receiveFunctionName(uint32_t typeId, const char* functionName,
                             const ELogFieldSpec& fieldSpec) override;

    /** @brief Receives the log msg. */
    void receiveLogMsg(uint32_t typeId, const char* logMsg,
                       const ELogFieldSpec& fieldSpec) override;

    /** @brief Receives a string log record field. */
    void receiveStringField(uint32_t typeId, const char* value, const ELogFieldSpec& fieldSpec,
                            size_t length) override;

    /** @brief Receives an integer log record field. */
    void receiveIntField(uint32_t typeId, uint64_t value, const ELogFieldSpec& fieldSpec) override;

    /** @brief Receives a time log record field. */
    void receiveTimeField(uint32_t typeId, const ELogTime& logTime, const char* timeStr,
                          const ELogFieldSpec& fieldSpec, size_t length) override;

    /** @brief Receives a log level log record field. */
    void receiveLogLevelField(uint32_t typeId, ELogLevel logLevel,
                              const ELogFieldSpec& fieldSpec) override;

private:
    elog_grpc::ELogRecordMsg* m_logRecordMsg;
};

}  // namespace elog

#endif  // ELOG_ENABLE_MSG

#endif  // __ELOG_PROTO_RECEPTOR_H__
