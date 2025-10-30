#ifndef __ELOG_PRE_INIT_LOGGER_H__
#define __ELOG_PRE_INIT_LOGGER_H__

#include <list>

#include "elog_logger.h"

namespace elog {

class ELogTarget;

/** @brief Special logger used for accumulating pre-init log messages. */
class ELogPreInitLogger : public ELogLogger {
public:
    ELogPreInitLogger() : ELogLogger(nullptr), m_recordBuilder(nullptr) {}
    ELogPreInitLogger(const ELogPreInitLogger&) = delete;
    ELogPreInitLogger(ELogPreInitLogger&&) = delete;
    ELogPreInitLogger& operator=(const ELogPreInitLogger&) = delete;
    ~ELogPreInitLogger() final { discardAccumulatedLogMessages(); }

    /** @brief Writes all accumulated log messages to the given log target. */
    void writeAccumulatedLogMessages(ELogTarget* logTarget);

    /** @brief Queries whether there are any accumulated log messages. */
    inline bool hasAccumulatedLogMessages() { return !m_accumulatedRecordBuilders.empty(); }

    /** @brief Retrieves the number of accumulated log messages. */
    inline uint32_t getAccumulatedMessageCount() const {
        return (uint32_t)m_accumulatedRecordBuilders.size();
    }

    /** @brief Discards all accumulated log messages. */
    void discardAccumulatedLogMessages();

protected:
    /** @brief Retrieves the underlying log record builder. */
    ELogRecordBuilder* getRecordBuilder() final;

    /** @brief Retrieves the underlying log record builder. */
    const ELogRecordBuilder* getRecordBuilder() const final;

    /** @brief Push current builder on builder stack and open a new builder. */
    ELogRecordBuilder* pushRecordBuilder() final;

    /** @brief Pop current builder from builder stack and restore previous builder. */
    void popRecordBuilder() final;

    /** @brief Finish logging (default behavior: finalize formatting and send to log target). */
    void finishLog(ELogRecordBuilder* recordBuilder) final;

private:
    ELogRecordBuilder* m_recordBuilder;
    std::list<ELogRecordBuilder*> m_accumulatedRecordBuilders;
};
}  // namespace elog

#endif  // __ELOG_PRE_INIT_LOGGER_H__