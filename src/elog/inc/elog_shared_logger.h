#ifndef __ELOG_SHARED_LOGGER_H__
#define __ELOG_SHARED_LOGGER_H__

#include "elog_logger.h"

namespace elog {

class ELOG_API ELogSharedLogger : public ELogLogger {
public:
    ELogSharedLogger(ELogSource* logSource) : ELogLogger(logSource) {}
    ELogSharedLogger(const ELogSharedLogger&) = delete;
    ELogSharedLogger(ELogSharedLogger&&) = delete;
    ELogSharedLogger& operator=(const ELogSharedLogger&) = delete;
    ~ELogSharedLogger() final {}

    /** @brief Allocate thread local storage key for per-thread record builder. */
    static bool createRecordBuilderKey();

    /** @brief Free thread local storage key used for per-thread record builder. */
    static bool destroyRecordBuilderKey();

protected:
    /** @brief Retrieves the underlying log record builder. */
    ELogRecordBuilder* getRecordBuilder() final;

    /** @brief Retrieves the underlying log record builder. */
    const ELogRecordBuilder* getRecordBuilder() const final;

    /** @brief Push current builder on builder stack and open a new builder. */
    ELogRecordBuilder* pushRecordBuilder() final;

    /** @brief Pop current builder from builder stack and restore previous builder. */
    void popRecordBuilder() final;
};
}  // namespace elog

#endif  // __ELOG_SHARED_LOGGER_H__