#ifndef __ELOG_FIELD_RECEPTOR_H__
#define __ELOG_FIELD_RECEPTOR_H__

#include <cstdint>
#include <string>

#include "elog_def.h"
#ifndef ELOG_MSVC
#include <sys/time.h>
#endif

#include "elog_level.h"

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
    virtual void receiveStaticText(uint32_t typeId, const std::string& text, int justify) {
        return receiveStringField(typeId, text, justify);
    }

    /** @brief Receives the log record id. */
    virtual void receiveRecordId(uint32_t typeId, uint64_t recordId, int justify) {
        return receiveIntField(typeId, recordId, justify);
    }

    /** @brief Receives the host name. */
    virtual void receiveHostName(uint32_t typeId, const std::string& hostName, int justify) {
        return receiveStringField(typeId, hostName, justify);
    }

    /** @brief Receives the user name. */
    virtual void receiveUserName(uint32_t typeId, const std::string& userName, int justify) {
        return receiveStringField(typeId, userName, justify);
    }

    /** @brief Receives the program name. */
    virtual void receiveProgramName(uint32_t typeId, const std::string& programName, int justify) {
        return receiveStringField(typeId, programName, justify);
    }

    /** @brief Receives the process id. */
    virtual void receiveProcessId(uint32_t typeId, uint64_t processId, int justify) {
        return receiveIntField(typeId, processId, justify);
    }

    /** @brief Receives the thread id. */
    virtual void receiveThreadId(uint32_t typeId, uint64_t threadId, int justify) {
        return receiveIntField(typeId, threadId, justify);
    }

    /** @brief Receives the thread name. */
    virtual void receiveThreadName(uint32_t typeId, const std::string& threadName, int justify) {
        return receiveStringField(typeId, threadName, justify);
    }

    /** @brief Receives the log source name. */
    virtual void receiveLogSourceName(uint32_t typeId, const std::string& logSourceName,
                                      int justify) {
        return receiveStringField(typeId, logSourceName, justify);
    }

    /** @brief Receives the module name. */
    virtual void receiveModuleName(uint32_t typeId, const std::string& moduleName, int justify) {
        return receiveStringField(typeId, moduleName, justify);
    }

    /** @brief Receives the file name. */
    virtual void receiveFileName(uint32_t typeId, const std::string& fileName, int justify) {
        return receiveStringField(typeId, fileName, justify);
    }

    /** @brief Receives the logging line. */
    virtual void receiveLineNumber(uint32_t typeId, uint64_t lineNumber, int justify) {
        return receiveIntField(typeId, lineNumber, justify);
    }

    /** @brief Receives the function name. */
    virtual void receiveFunctionName(uint32_t typeId, const std::string& functionName,
                                     int justify) {
        return receiveStringField(typeId, functionName, justify);
    }

    /** @brief Receives the log msg. */
    virtual void receiveLogMsg(uint32_t typeId, const std::string& logMsg, int justify) {
        return receiveStringField(typeId, logMsg, justify);
    }

    /**
     * Methods for receive by-type style.
     */

    /** @brief Receives a string log record field. */
    virtual void receiveStringField(uint32_t typeId, const std::string& value, int justify) = 0;

    /** @brief Receives an integer log record field. */
    virtual void receiveIntField(uint32_t typeId, uint64_t value, int justify) = 0;

#ifdef ELOG_MSVC
    /** @brief Receives a time log record field. */
    virtual void receiveTimeField(uint32_t typeId, const SYSTEMTIME& sysTime, const char* timeStr,
                                  int justify) = 0;
#else
    /** @brief Receives a time log record field. */
    virtual void receiveTimeField(uint32_t typeId, const timeval& sysTime, const char* timeStr,
                                  int justify) = 0;
#endif

    /** @brief Receives a log level log record field. */
    virtual void receiveLogLevelField(uint32_t typeId, ELogLevel logLevel, int justify) = 0;

protected:
    ELogFieldReceptor(ReceiveStyle receiveStyle = ReceiveStyle::RS_BY_TYPE)
        : m_receiveStyle(receiveStyle) {}

private:
    ReceiveStyle m_receiveStyle;
};

}  // namespace elog

#endif  // __ELOG_FIELD_RECEPTOR_H__