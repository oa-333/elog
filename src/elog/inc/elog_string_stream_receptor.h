#ifndef __ELOG_STRING_STREAM_RECEPTOR_H__
#define __ELOG_STRING_STREAM_RECEPTOR_H__

#include "elog_field_receptor.h"

namespace elog {

/**
 * @brief A default implementation of the @ref ELogFieldReceptor interface that redirects selected
 * log record fields into a string stream.
 */
class ELogStringStreamReceptor : public ELogFieldReceptor {
public:
    ELogStringStreamReceptor() {}
    ELogStringStreamReceptor(const ELogStringStreamReceptor&) = delete;
    ELogStringStreamReceptor(ELogStringStreamReceptor&&) = delete;
    ~ELogStringStreamReceptor() final {}

    /** @brief Receives a string log record field. */
    void receiveStringField(const std::string& field, int justify) final;

    /** @brief Receives an integer log record field. */
    void receiveIntField(uint64_t field, int justify) final;

#ifdef ELOG_MSVC
    /** @brief Receives a time log record field. */
    void receiveTimeField(const SYSTEMTIME& sysTime, const char* timeStr, int justify) final;
#else
    /** @brief Receives a time log record field. */
    void receiveTimeField(const timeval& sysTime, const char* timeStr, int justify) final;
#endif

    /** @brief Receives a log level log record field. */
    void receiveLogLevelField(ELogLevel logLevel, int justify) final;

    /** @brief Retrieves the formatted log message. */
    inline void getFormattedLogMsg(std::string& logMsg) const {
        logMsg = std::move(m_msgStream).str();
    }

private:
    std::stringstream m_msgStream;

    void applyJustify(int justify);
};

}  // namespace elog

#endif  // __ELOG_STRING_STREAM_RECEPTOR_H__