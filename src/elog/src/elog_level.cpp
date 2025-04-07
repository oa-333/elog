#include "elog_level.h"

#include <cstring>

namespace elog {

static ELogLevel gLogLevels[] = {ELOG_FATAL, ELOG_ERROR, ELOG_WARN,  ELOG_NOTICE,
                                 ELOG_INFO,  ELOG_TRACE, ELOG_DEBUG, ELOG_DIAG};

static const char* gLogLevelStr[] = {"FATAL", "ERROR", "WARN",  "NOTICE",
                                     "INFO",  "TRACE", "DEBUG", "DIAG"};

static const uint32_t gLogLevelCount = sizeof(gLogLevels) / sizeof(gLogLevels[0]);
static const uint32_t gLogLevelStrCount = sizeof(gLogLevelStr) / sizeof(gLogLevelStr[0]);

static_assert(gLogLevelCount == gLogLevelStrCount);

const char* elogLevelToStr(ELogLevel logLevel) {
    if (static_cast<uint32_t>(logLevel) <= gLogLevelCount) {
        return gLogLevelStr[logLevel];
    }
    return "N/A";
}

bool elogLevelFromStr(const char* logLevelStr, ELogLevel& logLevel) {
    for (uint32_t i = 0; i < gLogLevelCount; ++i) {
        if (strcmp(logLevelStr, gLogLevelStr[i]) == 0) {
            logLevel = gLogLevels[i];
            return true;
        }
    }
    return false;
}

}  // namespace elog
