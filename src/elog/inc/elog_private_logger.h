#ifndef __ELOG_PRIVATE_LOGGER_H__
#define __ELOG_PRIVATE_LOGGER_H__

#include "elog_logger.h"

namespace elog {

class ELOG_API ELogPrivateLogger : public ELogLogger {
public:
    ELogPrivateLogger(ELogSource* logSource)
        : ELogLogger(logSource), m_recordBuilder(&m_recordBuilderHead) {}
    ELogPrivateLogger(const ELogPrivateLogger&) = delete;
    ELogPrivateLogger(ELogPrivateLogger&&) = delete;
    ELogPrivateLogger& operator=(const ELogPrivateLogger&) = delete;
    ~ELogPrivateLogger() final {}

protected:
    /** @brief Retrieves the underlying log record builder. */
    ELogRecordBuilder* getRecordBuilder() final { return m_recordBuilder; }

    /** @brief Retrieves the underlying log record builder. */
    const ELogRecordBuilder* getRecordBuilder() const final { return m_recordBuilder; }

    /** @brief Push current builder on builder stack and open a new builder. */
    ELogRecordBuilder* pushRecordBuilder() final;

    /** @brief Pop current builder from builder stack and restore previous builder. */
    void popRecordBuilder() final;

private:
    ELogRecordBuilder m_recordBuilderHead;
    ELogRecordBuilder* m_recordBuilder;
};

}  // namespace elog

#endif  // __ELOG_PRIVATE_LOGGER_H__