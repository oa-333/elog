#ifndef __ELOG_STRING_RECEPTOR_H__
#define __ELOG_STRING_RECEPTOR_H__

#include <string>

#include "elog_field_receptor.h"

namespace elog {

/**
 * @brief A default implementation of the @ref ELogFieldReceptor interface that redirects selected
 * log record fields into a string (in-place, no string copying).
 */
class ELOG_API ELogStringReceptor : public ELogFieldReceptor {
public:
    ELogStringReceptor(std::string& logMsg) : m_logMsg(logMsg) {}
    ELogStringReceptor(const ELogStringReceptor&) = delete;
    ELogStringReceptor(ELogStringReceptor&&) = delete;
    ~ELogStringReceptor() final {}

    /** @brief Receives a string log record field. */
    void receiveStringField(uint32_t typeId, const char* field, const ELogFieldSpec& fieldSpec,
                            size_t length) final;

    /** @brief Receives an integer log record field. */
    void receiveIntField(uint32_t typeId, uint64_t field, const ELogFieldSpec& fieldSpec) final;

    /** @brief Receives a time log record field. */
    void receiveTimeField(uint32_t typeId, const ELogTime& logTime, const char* timeStr,
                          const ELogFieldSpec& fieldSpec) final;

    /** @brief Receives a log level log record field. */
    void receiveLogLevelField(uint32_t typeId, ELogLevel logLevel,
                              const ELogFieldSpec& fieldSpec) final;

private:
    std::string& m_logMsg;

    void applyJustify(const ELogFieldSpec& fieldSpec, const char* strField, uint32_t fieldLen = 0);
};

}  // namespace elog

#endif  // __ELOG_STRING_RECEPTOR_H__