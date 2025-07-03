#ifndef __ELOG_BUFFER_RECEPTOR_H__
#define __ELOG_BUFFER_RECEPTOR_H__

#include "elog_buffer.h"
#include "elog_field_receptor.h"

namespace elog {

/**
 * @brief A default implementation of the @ref ELogFieldReceptor interface that redirects selected
 * log record fields into a string (in-place, no string copying).
 */
class ELOG_API ELogBufferReceptor : public ELogFieldReceptor {
public:
    ELogBufferReceptor(ELogBuffer& logBuffer) : m_buffer(logBuffer) {}
    ELogBufferReceptor(const ELogBufferReceptor&) = delete;
    ELogBufferReceptor(ELogBufferReceptor&&) = delete;
    ~ELogBufferReceptor() final {}

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

    inline void finalize() { m_buffer.finalize(); }
    inline const char* getBuffer() const { return m_buffer.getRef(); }
    inline size_t getBufferSize() const { return m_buffer.getOffset(); }

private:
    ELogBuffer& m_buffer;

    void applySpec(const ELogFieldSpec& fieldSpec, const char* strField, uint32_t fieldLen = 0);
};

}  // namespace elog

#endif  // __ELOG_BUFFER_RECEPTOR_H__