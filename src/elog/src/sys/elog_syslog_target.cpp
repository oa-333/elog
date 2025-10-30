#include "sys/elog_syslog_target.h"

#ifdef ELOG_LINUX

#include <syslog.h>

#include "elog_field_selector.h"
#include "elog_field_selector_internal.h"

namespace elog {

ELOG_IMPLEMENT_LOG_TARGET(ELogSysLogTarget)

bool ELogSysLogTarget::startLogTarget() {
    openlog(getProgramNameField(), LOG_PID, LOG_USER);
    return true;
}

bool ELogSysLogTarget::stopLogTarget() {
    closelog();
    return true;
}

uint32_t ELogSysLogTarget::writeLogRecord(const ELogRecord& logRecord) {
    // first translate log level
    int sysLevel = 0;
    if (logLevelToSysLevel(logRecord.m_logLevel, sysLevel)) {
        std::string logMsg;
        formatLogMsg(logRecord, logMsg);
        syslog(sysLevel | LOG_USER, "%s", logMsg.c_str());
        return logMsg.length();
    }
    return 0;
}

bool ELogSysLogTarget::flushLogTarget() { return true; }

bool ELogSysLogTarget::logLevelToSysLevel(ELogLevel logLevel, int& sysLevel) {
    switch (logLevel) {
        case ELEVEL_FATAL:
            sysLevel = LOG_CRIT;
            break;

        case ELEVEL_ERROR:
            sysLevel = LOG_ERR;
            break;

        case ELEVEL_WARN:
            sysLevel = LOG_WARNING;
            break;

        case ELEVEL_NOTICE:
            sysLevel = LOG_NOTICE;
            break;

        case ELEVEL_INFO:
            sysLevel = LOG_INFO;
            break;

        case ELEVEL_TRACE:
        case ELEVEL_DEBUG:
            sysLevel = LOG_DEBUG;
            break;

        case ELEVEL_DIAG:
        default:
            // NOTE: diagnostic level is not reported to avoid syslog flooding
            return false;
    }
    return true;
}

}  // namespace elog

#endif  // ELOG_LINUX