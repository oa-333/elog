#ifndef __ELOG_ERROR_HANDLER_H__
#define __ELOG_ERROR_HANDLER_H__

#include <atomic>
#include <string>

#include "elog_def.h"
#include "elog_level.h"

namespace elog {

// forward declaration
class ELOG_API ELogLogger;

/** @brief ELog's internal reporting logger. */
class ELOG_API ELogReportLogger {
public:
    ELogReportLogger(const char* name)
        : m_name(name), m_logger(nullptr), m_initState(InitState::IS_NO_INIT) {}
    ELogReportLogger(const ELogReportLogger&) = delete;
    ELogReportLogger(ELogReportLogger&&) = delete;
    ELogReportLogger& operator=(const ELogReportLogger&) = delete;
    ~ELogReportLogger() {}

    /**
     * @brief Initializes the logger explicitly before use, so that all required resources and
     * members (i.e. log source, logger) are ready for use. If this call is omitted, then the
     * required resources will be created on-demand (still thread-safe with some contention).
     * @note This is not related to the external initialization API below.
     */
    bool initialize();

    /** @brief Retrieves the name of the logger. */
    inline const char* getName() const { return m_name.c_str(); }

    /**
     * @brief Retrieves the associated logger (used by ELog self/internal logging). This may cause
     * creation on demand of log source. In order to avoid that, call explicitly @ref initialize().
     */
    ELogLogger* getLogger();

    /**
     * @brief Queries whether external initialization for the logger has not taken place yet.
     * @note This API was added to allow managing external initialization state on the logger level,
     * so that an external global map can be avoided.
     * @return True if external initialization has not taken place yet.
     */
    inline bool requiresInit() const {
        return m_initState.load(std::memory_order_relaxed) == InitState::IS_NO_INIT;
    }

    /**
     * @brief Attempts to start external initialization for the logger. The call may fail due to
     * race conditions (i.e. another thread is also trying to execute external initialization for
     * the report logger).
     * @note This is just an atomic state change. This API was added to allow managing external
     * initialization state on the logger level, so that an external global map can be avoided.
     * @return True if the caller succeeded in changing the initialization state for the logger. The
     * caller can now execute external initialization for the logger, and call @ref finishInit()
     * when it is done.
     */
    bool startInit();

    /**
     * @brief Signals external initialization has finished.
     * @note This is just an atomic state change. This API was added to allow managing external
     * initialization state on the logger level, so that an external global map can be avoided.
     */
    inline void finishInit() { m_initState.store(InitState::IS_INIT, std::memory_order_relaxed); }

    /**
     * @brief Busy-waits for external initialization to finish.
     * @note This is a busy-wait loop (with CPU yield in-between), allowing contending threads to
     * wait for the logger to be ready after external initialization finished.
     */
    void waitFinishInit() const;

private:
    std::string m_name;
    ELogLogger* m_logger;
    enum class InitState : uint32_t { IS_NO_INIT, IS_DURING_INIT, IS_INIT };
    std::atomic<InitState> m_initState;
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