#include "msg/elog_proto_receptor.h"

#ifdef ELOG_ENABLE_MSG

namespace elog {

void ELogProtoReceptor::receiveStaticText(uint32_t typeId, const std::string& text,
                                          const ELogFieldSpec& fieldSpec) {
    // static text is not used, just discard it
}

void ELogProtoReceptor::receiveRecordId(uint32_t typeId, uint64_t recordId,
                                        const ELogFieldSpec& fieldSpec) {
    m_logRecordMsg->set_recordid(recordId);
}

void ELogProtoReceptor::receiveHostName(uint32_t typeId, const char* hostName,
                                        const ELogFieldSpec& fieldSpec) {
    m_logRecordMsg->set_hostname(hostName);
}

void ELogProtoReceptor::receiveUserName(uint32_t typeId, const char* userName,
                                        const ELogFieldSpec& fieldSpec) {
    m_logRecordMsg->set_username(userName);
}

void ELogProtoReceptor::receiveProgramName(uint32_t typeId, const char* programName,
                                           const ELogFieldSpec& fieldSpec) {
    m_logRecordMsg->set_programname(programName);
}

void ELogProtoReceptor::receiveProcessId(uint32_t typeId, uint64_t processId,
                                         const ELogFieldSpec& fieldSpec) {
    m_logRecordMsg->set_processid(processId);
}

void ELogProtoReceptor::receiveThreadId(uint32_t typeId, uint64_t threadId,
                                        const ELogFieldSpec& fieldSpec) {
    m_logRecordMsg->set_threadid(threadId);
}

void ELogProtoReceptor::receiveThreadName(uint32_t typeId, const char* threadName,
                                          const ELogFieldSpec& fieldSpec) {
    m_logRecordMsg->set_threadname(threadName);
}

void ELogProtoReceptor::receiveLogSourceName(uint32_t typeId, const char* logSourceName,
                                             const ELogFieldSpec& fieldSpec) {
    m_logRecordMsg->set_logsourcename(logSourceName);
}

void ELogProtoReceptor::receiveModuleName(uint32_t typeId, const char* moduleName,
                                          const ELogFieldSpec& fieldSpec) {
    m_logRecordMsg->set_modulename(moduleName);
}

void ELogProtoReceptor::receiveFileName(uint32_t typeId, const char* fileName,
                                        const ELogFieldSpec& fieldSpec) {
    m_logRecordMsg->set_file(fileName);
}

void ELogProtoReceptor::receiveLineNumber(uint32_t typeId, uint64_t lineNumber,
                                          const ELogFieldSpec& fieldSpec) {
    m_logRecordMsg->set_line((uint32_t)lineNumber);
}

void ELogProtoReceptor::receiveFunctionName(uint32_t typeId, const char* functionName,
                                            const ELogFieldSpec& fieldSpec) {
    m_logRecordMsg->set_functionname(functionName);
}

void ELogProtoReceptor::receiveLogMsg(uint32_t typeId, const char* logMsg,
                                      const ELogFieldSpec& fieldSpec) {
    m_logRecordMsg->set_logmsg(logMsg);
}

void ELogProtoReceptor::receiveStringField(uint32_t typeId, const char* value,
                                           const ELogFieldSpec& fieldSpec, size_t length) {
    // if external fields are used, then derive from the receptor and transfer the extra fields into
    // the log message
}

void ELogProtoReceptor::receiveIntField(uint32_t typeId, uint64_t value,
                                        const ELogFieldSpec& fieldSpec) {
    // if external fields are used, then derive from the receptor and transfer the extra fields into
    // the log message
}

void ELogProtoReceptor::receiveTimeField(uint32_t typeId, const ELogTime& logTime,
                                         const char* timeStr, const ELogFieldSpec& fieldSpec,
                                         size_t length) {
    uint64_t unixTimeMillis = elogTimeToUnixTimeNanos(logTime) / 1000000ULL;
    m_logRecordMsg->set_timeunixepochmillis(unixTimeMillis);
}

void ELogProtoReceptor::receiveLogLevelField(uint32_t typeId, ELogLevel logLevel,
                                             const ELogFieldSpec& fieldSpec) {
    m_logRecordMsg->set_loglevel((uint32_t)logLevel);
}

}  // namespace elog

#endif  // ELOG_ENABLE_MSG
