#ifndef __ELOG_LEVEL_H__
#define __ELOG_LEVEL_H__

#include <cstdint>

namespace elog {

/** @enum Log level constants. */
enum ELogLevel : uint32_t {
    /**
     * @var Fatal log level. Application cannot continue operation and will terminate by itself or
     * abruptly crash.
     */
    ELEVEL_FATAL,

    /** @var Error log level. An error condition occurred. Application can continue operating. */
    ELEVEL_ERROR,

    /**
     * @var Warning log level. User is warned about some error condition, but not as severe as error
     * log level.
     */
    ELEVEL_WARN,

    /**
     * @var Notice log level. User should note about some condition. It is not an error. Usually
     * application can cope with it, but there might be some implications (e.g. reduced
     * performance).
     */
    ELEVEL_NOTICE,

    /** @var Informative log level. Should be used to log infrequent important details. */
    ELEVEL_INFO,

    /** @var Trace log level. Used for debugging not so noisy components. */
    ELEVEL_TRACE,

    /** @var Trace log level. Used for debugging noisy components. */
    ELEVEL_DEBUG,

    /** @var Trace log level. Used for debugging very noisy components. Log flooding is expected. */
    ELEVEL_DIAG
};

/** @brief Converts log level constant to string. */
extern const char* elogLevelToStr(ELogLevel logLevel);

/** @brief Converts log level string to log level constant. */
extern bool elogLevelFromStr(const char* logLevelStr, ELogLevel& logLevel);

}  // namespace elog

#endif  // __ELOG_LEVEL_H__