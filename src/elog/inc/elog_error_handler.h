#ifndef __ELOG_ERROR_HANDLER_H__
#define __ELOG_ERROR_HANDLER_H__

#include "elog_def.h"
#include "elog_level.h"

namespace elog {

/**
 * @brief ELog internal message report handling interface. User can derive, implement and pass to
 * ELog initialization function.
 * @see @ref elog::initialize().
 */
class ELOG_API ELogReportHandler {
public:
    /** @brief Disable copy constructor. */
    ELogReportHandler(const ELogReportHandler&) = delete;

    /** @brief Disable move constructor. */
    ELogReportHandler(ELogReportHandler&&) = delete;

    /** @brief Disable assignment operator. */
    ELogReportHandler& operator=(const ELogReportHandler&) = delete;

    /** @brief Destructor. */
    virtual ~ELogReportHandler() {}

    /** @brief Reports ELog internal log message. */
    virtual void onReportV(ELogLevel logLevel, const char* file, int line, const char* function,
                           const char* fmt, va_list args) = 0;

    /** @brief Reports ELog internal log message. */
    virtual void onReport(ELogLevel logLevel, const char* file, int line, const char* function,
                          const char* msg) = 0;

    /** @brief Configures elog report level. */
    virtual void setReportLevel(ELogLevel reportLevel) { m_reportLevel = reportLevel; }

    /** @brief Retrieves report level. */
    inline ELogLevel getReportLevel() { return m_reportLevel; }

    /** @brief Queries whether trace mode is enabled. */
    inline bool isTraceEnabled() { return m_reportLevel >= ELEVEL_TRACE; }

protected:
    /** @brief Constructor. */
    ELogReportHandler(ELogLevel reportLevel = ELEVEL_WARN) : m_reportLevel(reportLevel) {}

private:
    ELogLevel m_reportLevel;
};

}  // namespace elog

#endif  // __ELOG_ERROR_HANDLER_H__