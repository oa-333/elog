#ifndef __ELOG_HTTP_CONFIG_H__
#define __ELOG_HTTP_CONFIG_H__

#include <cstdint>

#include "elog_def.h"

/** @def By default wait for 5 seconds before declaring connection failure. */
#define ELOG_HTTP_DEFAULT_CONNECT_TIMEOUT_MILLIS 200

/** @def By default wait for 5 seconds before declaring write failure. */
#define ELOG_HTTP_DEFAULT_WRITE_TIMEOUT_MILLIS 50

/** @def By default wait for 5 seconds before declaring read failure. */
#define ELOG_HTTP_DEFAULT_READ_TIMEOUT_MILLIS 100

/** @def By default wait for 5 seconds before trying to resend failed HTTP messages. */
#define ELOG_HTTP_DEFAULT_RESEND_TIMEOUT_MILLIS 5000

/** @def By default allow for a total 1 MB of payload to be backlogged for resend. */
#define ELOG_HTTP_DEFAULT_BACKLOG_LIMIT_BYTES (1024 * 1024)

/**
 * @def By default wait for 5 seconds before trying to resend failed HTTP messages during
 * shutdown.
 */
#define ELOG_HTTP_DEFAULT_SHUTDOWN_TIMEOUT_MILLIS 5000

namespace elog {

/** @brief Pack all HTTP configuration in one place. */
struct ELOG_API ELogHttpConfig {
    /** @brief The timeout for HTTP connect to be declared as failed. */
    uint32_t m_connectTimeoutMillis;

    /** @brief The timeout for HTTP write to be declared as failed. */
    uint32_t m_writeTimeoutMillis;

    /** @brief The timeout for HTTP read to be declared as failed. */
    uint32_t m_readTimeoutMillis;

    /** @brief The interval between resend attempt of previously failed HTTP message send. */
    uint32_t m_resendPeriodMillis;

    /** @brief The size limit of the backlog used for storing messages after failed send attempt. */
    uint32_t m_backlogLimitBytes;

    /** @brief The timeout used for final attempt to resend all unsent HTTP messages. */
    uint32_t m_shutdownTimeoutMillis;

    ELogHttpConfig()
        : m_connectTimeoutMillis(ELOG_HTTP_DEFAULT_CONNECT_TIMEOUT_MILLIS),
          m_writeTimeoutMillis(ELOG_HTTP_DEFAULT_WRITE_TIMEOUT_MILLIS),
          m_readTimeoutMillis(ELOG_HTTP_DEFAULT_READ_TIMEOUT_MILLIS),
          m_resendPeriodMillis(ELOG_HTTP_DEFAULT_RESEND_TIMEOUT_MILLIS),
          m_backlogLimitBytes(ELOG_HTTP_DEFAULT_BACKLOG_LIMIT_BYTES),
          m_shutdownTimeoutMillis(ELOG_HTTP_DEFAULT_SHUTDOWN_TIMEOUT_MILLIS) {}
};

}  // namespace elog

#endif  // __ELOG_HTTP_CONFIG_H__