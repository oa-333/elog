#ifndef __ELOG_FIELD_RECEPTOR_H__
#define __ELOG_FIELD_RECEPTOR_H__

#include <cstdint>
#include <string>

#include "elog_def.h"
#ifndef ELOG_MSVC
#include <sys/time.h>
#endif

#include "elog_field_spec.h"
#include "elog_level.h"
#include "elog_record.h"

namespace elog {

/** @brief Parent interface for the target receptor of selected log record fields. */
class ELOG_API ELogFieldReceptor {
public:
    virtual ~ELogFieldReceptor() {}

    /**
     * @enum Constants denoting how does the receptor prefer to receive the selected log record
     * fields.
     */
    enum class ReceiveStyle {
        /**
         * @var Denotes receptor prefers receiving log record fields by data type. When this is
         * used, the receptor will receive all fields via @ref receiveStringField() and such.
         */
        RS_BY_TYPE,

        /**
         * @var Denotes receptor prefers receiving log record fields by field name. When this is
         * used, the receptor will receive all predefined fields via @ref receiveRecordId() and
         * such, while for external fields, the receive style will fall back to by-type. An
         * exception to this is when receiving time and log level fields. In this case the same
         * reception method is used, regardless of receive style. In particular, @ref
         * receiveTimeField(), and @ref receiveLogLevelField() are called in both cases.
         */
        RS_BY_NAME
    };

    /** @brief Queries the receptor's preferred field receive style. */
    inline ReceiveStyle getFieldReceiveStyle() const { return m_receiveStyle; }

    /**
     * Methods for receive by-name style. In order to avoid double implementation by receptors of
     * one style, and since most receptors use by-type style, this methods have a default
     * implementation which redirects to by-type methods.
     */

    /** @brief Receives any static text found outside of log record field references. */
    virtual void receiveStaticText(uint32_t typeId, const std::string& text,
                                   const ELogFieldSpec& fieldSpec) {
        return receiveStringField(typeId, text.c_str(), fieldSpec);
    }

    /** @brief Receives the log record id. */
    virtual void receiveRecordId(uint32_t typeId, uint64_t recordId,
                                 const ELogFieldSpec& fieldSpec) {
        return receiveIntField(typeId, recordId, fieldSpec);
    }

    /** @brief Receives the host name. */
    virtual void receiveHostName(uint32_t typeId, const char* hostName,
                                 const ELogFieldSpec& fieldSpec) {
        return receiveStringField(typeId, hostName, fieldSpec);
    }

    /** @brief Receives the user name. */
    virtual void receiveUserName(uint32_t typeId, const char* userName,
                                 const ELogFieldSpec& fieldSpec) {
        return receiveStringField(typeId, userName, fieldSpec);
    }

    /** @brief Receives the program name. */
    virtual void receiveProgramName(uint32_t typeId, const char* programName,
                                    const ELogFieldSpec& fieldSpec) {
        return receiveStringField(typeId, programName, fieldSpec);
    }

    /** @brief Receives the process id. */
    virtual void receiveProcessId(uint32_t typeId, uint64_t processId,
                                  const ELogFieldSpec& fieldSpec) {
        return receiveIntField(typeId, processId, fieldSpec);
    }

    /** @brief Receives the thread id. */
    virtual void receiveThreadId(uint32_t typeId, uint64_t threadId,
                                 const ELogFieldSpec& fieldSpec) {
        return receiveIntField(typeId, threadId, fieldSpec);
    }

    /** @brief Receives the thread name. */
    virtual void receiveThreadName(uint32_t typeId, const char* threadName,
                                   const ELogFieldSpec& fieldSpec) {
        return receiveStringField(typeId, threadName, fieldSpec);
    }

    /** @brief Receives the log source name. */
    virtual void receiveLogSourceName(uint32_t typeId, const char* logSourceName,
                                      const ELogFieldSpec& fieldSpec) {
        return receiveStringField(typeId, logSourceName, fieldSpec);
    }

    /** @brief Receives the module name. */
    virtual void receiveModuleName(uint32_t typeId, const char* moduleName,
                                   const ELogFieldSpec& fieldSpec) {
        return receiveStringField(typeId, moduleName, fieldSpec);
    }

    /** @brief Receives the file name. */
    virtual void receiveFileName(uint32_t typeId, const char* fileName,
                                 const ELogFieldSpec& fieldSpec) {
        return receiveStringField(typeId, fileName, fieldSpec);
    }

    /** @brief Receives the logging line. */
    virtual void receiveLineNumber(uint32_t typeId, uint64_t lineNumber,
                                   const ELogFieldSpec& fieldSpec) {
        return receiveIntField(typeId, lineNumber, fieldSpec);
    }

    /** @brief Receives the function name. */
    virtual void receiveFunctionName(uint32_t typeId, const char* functionName,
                                     const ELogFieldSpec& fieldSpec) {
        return receiveStringField(typeId, functionName, fieldSpec);
    }

    /** @brief Receives the log msg. */
    virtual void receiveLogMsg(uint32_t typeId, const char* logMsg,
                               const ELogFieldSpec& fieldSpec) {
        return receiveStringField(typeId, logMsg, fieldSpec);
    }

    /**
     * Methods for receive by-type style.
     */

    /** @brief Receives a string log record field. */
    virtual void receiveStringField(uint32_t typeId, const char* value,
                                    const ELogFieldSpec& fieldSpec, size_t length = 0) = 0;

    /** @brief Receives an integer log record field. */
    virtual void receiveIntField(uint32_t typeId, uint64_t value,
                                 const ELogFieldSpec& fieldSpec) = 0;

    /** @brief Receives a time log record field. */
    virtual void receiveTimeField(uint32_t typeId, const ELogTime& logTime, const char* timeStr,
                                  const ELogFieldSpec& fieldSpec) = 0;

    /** @brief Receives a log level log record field. */
    virtual void receiveLogLevelField(uint32_t typeId, ELogLevel logLevel,
                                      const ELogFieldSpec& fieldSpec) = 0;

protected:
    ELogFieldReceptor(ReceiveStyle receiveStyle = ReceiveStyle::RS_BY_TYPE)
        : m_receiveStyle(receiveStyle) {}

private:
    ReceiveStyle m_receiveStyle;
};

}  // namespace elog

#endif  // __ELOG_FIELD_RECEPTOR_H__