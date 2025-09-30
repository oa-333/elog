#ifndef __ELOG_MSG_CONFIG_H__
#define __ELOG_MSG_CONFIG_H__

#include <msg/msg_config.h>

#include <string>

#include "msg/elog_binary_format_provider.h"

/** @brief Default sync mode for message-based log targets. */
#define ELOG_MSG_DEFAULT_SYNC_MODE true

/** @brief Default compression configuration for message-based log targets. */
#define ELOG_MSG_DEFAULT_COMPRESS false

/** @brief Min/Max/Default concurrent requests configuration for message-based log targets. */
#define ELOG_MSG_MIN_CONCURRENT_REQUESTS 1
#define ELOG_MSG_MAX_CONCURRENT_REQUESTS 4096
#define ELOG_MSG_DEFAULT_CONCURRENT_REQUESTS 32

/** @brief Default binary format for message-based log targets. */
#define ELOG_MSG_DEFAULT_BINARY_FORMAT "protobuf"

// all timeouts are in milliseconds

/** @brief Min/Max/Default connect timeout for message-based log targets. */
#define ELOG_MSG_MIN_CONNECT_TIMEOUT 50
#define ELOG_MSG_MAX_CONNECT_TIMEOUT 30000
#define ELOG_MSG_DEFAULT_CONNECT_TIMEOUT 5000

/** @brief Min/Max/Default send timeout for message-based log targets. */
#define ELOG_MSG_MIN_SEND_TIMEOUT 50
#define ELOG_MSG_MAX_SEND_TIMEOUT 30000
#define ELOG_MSG_DEFAULT_SEND_TIMEOUT 1000

/** @brief Min/Max/Default resend timeout for message-based log targets. */
#define ELOG_MSG_MIN_RESEND_TIMEOUT 50
#define ELOG_MSG_MAX_RESEND_TIMEOUT 60000
#define ELOG_MSG_DEFAULT_RESEND_TIMEOUT 5000

/** @brief Min/Max/Default expire timeout for message-based log targets. */
#define ELOG_MSG_MIN_EXPIRE_TIMEOUT 50
#define ELOG_MSG_MAX_EXPIRE_TIMEOUT (5 * 60000)
#define ELOG_MSG_DEFAULT_EXPIRE_TIMEOUT 30000

/** @brief Min/Max/Default backlog size for message-based log targets. */
#define ELOG_MSG_MIN_BACKLOG_SIZE 1024ull
#define ELOG_MSG_MAX_BACKLOG_SIZE (64ull * 1024ull * 1024ull)
#define ELOG_MSG_DEFAULT_BACKLOG_SIZE (1024ull * 1024ull)

/** @brief Min/Max/Default shutdown timeout for message-based log targets. */
#define ELOG_MSG_MIN_SHUTDOWN_TIMEOUT 50
#define ELOG_MSG_MAX_SHUTDOWN_TIMEOUT 30000
#define ELOG_MSG_DEFAULT_SHUTDOWN_TIMEOUT 5000

/** @brief Min/Max/Default shutdown polling timeout for message-based log targets. */
#define ELOG_MSG_MIN_SHUTDOWN_POLLING_TIMEOUT 10
#define ELOG_MSG_MAX_SHUTDOWN_POLLING_TIMEOUT 1000
#define ELOG_MSG_DEFAULT_SHUTDOWN_POLLING_TIMEOUT 50

namespace elog {

/** @brief Common configuration for all message-based log targets. */
struct ELogMsgConfig {
    /** @var Specifies whether communication is synchronous (blocking) or not.  */
    bool m_syncMode;

    /** @var Specifies whether outgoing messages should be compressed or not. */
    bool m_compress;

    /** @var Specifies the maximum allowed number of outstanding pending requests. */
    uint32_t m_maxConcurrentRequests;

    /** @brief Specifies the binary format used in serializing log records. */
    ELogBinaryFormatProvider* m_binaryFormatProvider;

    /** @var Communication/transport layer configuration. */
    commutil::MsgConfig m_commConfig;
};

}  // namespace elog

#endif  // __ELOG_MSG_CONFIG_H__