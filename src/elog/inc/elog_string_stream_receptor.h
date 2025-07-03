#ifndef __ELOG_STRING_STREAM_RECEPTOR_H__
#define __ELOG_STRING_STREAM_RECEPTOR_H__

#include <sstream>

#include "elog_field_receptor.h"

namespace elog {

/**
 * @brief A default implementation of the @ref ELogFieldReceptor interface that redirects selected
 * log record fields into a string stream.
 */
class ELOG_API ELogStringStreamReceptor : public ELogFieldReceptor {
public:
    ELogStringStreamReceptor() {}
    ELogStringStreamReceptor(std::string& logMsg) : m_msgStream(logMsg) {}
    ELogStringStreamReceptor(const ELogStringStreamReceptor&) = delete;
    ELogStringStreamReceptor(ELogStringStreamReceptor&&) = delete;
    ~ELogStringStreamReceptor() final {}

    /** @brief Receives a string log record field. */
    void receiveStringField(uint32_t typeId, const char* field, const ELogFieldSpec& fieldSpec,
                            size_t length) final;

    /** @brief Receives an integer log record field. */
    void receiveIntField(uint32_t typeId, uint64_t field, const ELogFieldSpec& fieldSpec) final;

    /** @brief Receives a time log record field. */
    void receiveTimeField(uint32_t typeId, const ELogTime& logTime, const char* timeStr,
                          const ELogFieldSpec& fieldSpec, size_t length) final;

    /** @brief Receives a log level log record field. */
    void receiveLogLevelField(uint32_t typeId, ELogLevel logLevel,
                              const ELogFieldSpec& fieldSpec) final;

    /** @brief Retrieves the formatted log message. */
    inline void getFormattedLogMsg(std::string& logMsg) const {
        logMsg = std::move(m_msgStream).str();
    }

private:
    std::stringstream m_msgStream;

    void applySpec(const ELogFieldSpec& fieldSpec);
    void applyPostSpec(const ELogFieldSpec& fieldSpec);
};

}  // namespace elog

#endif  // __ELOG_STRING_STREAM_RECEPTOR_H__