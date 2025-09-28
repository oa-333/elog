#ifndef __ELOG_LEVEL_CFG_H__
#define __ELOG_LEVEL_CFG_H__

#include "elog_common_def.h"
#include "elog_level.h"

#ifdef ELOG_USING_DBG_UTIL
#include "dbg_util.h"
#endif

#ifdef ELOG_USING_COMM_UTIL
#include "comm_util.h"
#endif

namespace elog {

// forward declaration
class ELOG_API ELogSource;

/** @brief Log level configuration used for delayed log level propagation. */
struct ELogLevelCfg {
    ELogLevelCfg(ELogSource* logSource = nullptr, ELogLevel logLevel = ELEVEL_INFO,
                 ELogPropagateMode propagateMode = ELogPropagateMode::PM_NONE)
        : m_logSource(logSource), m_logLevel(logLevel), m_propagationMode(propagateMode) {}
    ELogLevelCfg(const ELogLevelCfg&) = default;
    ELogLevelCfg(ELogLevelCfg&&) = default;
    ELogLevelCfg& operator=(const ELogLevelCfg&) = default;
    ~ELogLevelCfg() {}

    /** @var The configures log source. */
    ELogSource* m_logSource;

    /** @var The log level to set. */
    ELogLevel m_logLevel;

    /** @var Controls how the log level affects the child log sources. */
    ELogPropagateMode m_propagationMode;
};

#ifdef ELOG_USING_DBG_UTIL
/** @brief Log level configuration used for delayed log level propagation. */
struct ELogDbgLevelCfg : public ELogLevelCfg {
    ELogDbgLevelCfg(ELogSource* logSource = nullptr, ELogLevel logLevel = ELEVEL_INFO,
                    ELogPropagateMode propagateMode = ELogPropagateMode::PM_NONE,
                    size_t loggerId = 0,
                    dbgutil::LogSeverity severity = dbgutil::LogSeverity::LS_INFO)
        : ELogLevelCfg(logSource, logLevel, propagateMode),
          m_loggerId(loggerId),
          m_severity(severity) {}
    ELogDbgLevelCfg(const ELogDbgLevelCfg&) = default;
    ELogDbgLevelCfg(ELogDbgLevelCfg&&) = default;
    ELogDbgLevelCfg& operator=(const ELogDbgLevelCfg&) = default;
    ~ELogDbgLevelCfg() {}

    /** @var Identifies the origin Debug Utilities logger. */
    size_t m_loggerId;

    /** @var Specifies the configured severity of the Debug Utilities logger. */
    dbgutil::LogSeverity m_severity;
};
#endif

#ifdef ELOG_USING_COMM_UTIL
/** @brief Log level configuration used for delayed log level propagation. */
struct ELogCommLevelCfg : public ELogLevelCfg {
    ELogCommLevelCfg(ELogSource* logSource = nullptr, ELogLevel logLevel = ELEVEL_INFO,
                     ELogPropagateMode propagateMode = ELogPropagateMode::PM_NONE,
                     size_t loggerId = 0,
                     commutil::LogSeverity severity = commutil::LogSeverity::LS_INFO)
        : ELogLevelCfg(logSource, logLevel, propagateMode),
          m_loggerId(loggerId),
          m_severity(severity) {}
    ELogCommLevelCfg(const ELogCommLevelCfg&) = default;
    ELogCommLevelCfg(ELogCommLevelCfg&&) = default;
    ELogCommLevelCfg& operator=(const ELogCommLevelCfg&) = default;
    ~ELogCommLevelCfg() {}

    /** @var Identifies the origin Communication Utilities logger. */
    size_t m_loggerId;

    /** @var Specifies the configured severity of the Communication Utilities logger. */
    commutil::LogSeverity m_severity;
};
#endif

}  // namespace elog

#endif  // __ELOG_LEVEL_CFG_H__