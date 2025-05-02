#include "elog_level.h"

#include <cstring>

namespace elog {

static ELogLevel gLogLevels[] = {ELEVEL_FATAL, ELEVEL_ERROR, ELEVEL_WARN,  ELEVEL_NOTICE,
                                 ELEVEL_INFO,  ELEVEL_TRACE, ELEVEL_DEBUG, ELEVEL_DIAG};

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

bool elogLevelFromStr(const char* logLevelStr, ELogLevel& logLevel,
                      const char** ptr /* = nullptr */) {
    for (uint32_t i = 0; i < gLogLevelCount; ++i) {
        if (strncmp(logLevelStr, gLogLevelStr[i], strlen(gLogLevelStr[i])) == 0) {
            logLevel = gLogLevels[i];
            if (ptr) {
                *ptr = logLevelStr + strlen(gLogLevelStr[i]);
            }
            return true;
        }
    }
    return false;
}

}  // namespace elog
