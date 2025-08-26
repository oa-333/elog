#ifndef __ELOG_LEVEL_CFG_H__
#define __ELOG_LEVEL_CFG_H__

#include "elog_common_def.h"
#include "elog_level.h"

#ifdef ELOG_ENABLE_STACK_TRACE
#include "dbg_util.h"
#endif

namespace elog {

// forward declaration
class ELOG_API ELogSource;

/** @brief Log level configuration used for delayed log level propagation. */
struct ELogLevelCfg {
    /** @var The configures log source. */
    ELogSource* m_logSource;

    /** @var The log level to set. */
    ELogLevel m_logLevel;

    /** @var Controls how the log level affects the child log sources. */
    ELogPropagateMode m_propagationMode;

#ifdef ELOG_USING_DBG_UTIL
    /** @var Identifies the origin Debug Utilities logger. */
    size_t m_dbgUtilLoggerId;

    /** @var Specifies the configured severity of the Debug Utilities logger. */
    dbgutil::LogSeverity m_severity;
#endif
};

}  // namespace elog

#endif  // __ELOG_LEVEL_CFG_H__