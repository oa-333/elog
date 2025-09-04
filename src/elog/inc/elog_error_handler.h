#ifndef __ELOG_ERROR_HANDLER_H__
#define __ELOG_ERROR_HANDLER_H__

#include <string>

#include "elog_def.h"
#include "elog_level.h"

namespace elog {

// forward declaration
class ELOG_API ELogLogger;

/** @brief ELog's internal reporting logger. */
class ELOG_API ELogReportLogger {
public:
    ELogReportLogger(const char* name) : m_name(name), m_logger(nullptr) {}
    ELogReportLogger(const ELogReportLogger&) = delete;
    ELogReportLogger(ELogReportLogger&&) = delete;
    ELogReportLogger& operator=(const ELogReportLogger&) = delete;
    ~ELogReportLogger() {}

    inline const char* getName() const { return m_name.c_str(); }

    ELogLogger* getLogger(bool& logSourceCreated) const;

private:
    std::string m_name;
    mutable ELogLogger* m_logger;
};

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
    virtual void onReportV(const ELogReportLogger& reportLogger, ELogLevel logLevel,
                           const char* file, int line, const char* function, const char* fmt,
                           va_list args) = 0;

    /** @brief Reports ELog internal log message. */
    virtual void onReport(const ELogReportLogger& reportLogger, ELogLevel logLevel,
                          const char* file, int line, const char* function, const char* msg) = 0;

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