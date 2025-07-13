#ifndef __ELOG_HTTP_CLIENT_H__
#define __ELOG_HTTP_CLIENT_H__

#ifdef ELOG_ENABLE_HTTP

#include <httplib.h>

#include <gzip/compress.hpp>

#include "elog_def.h"

namespace elog {

/** @def By default wait for 5 seconds before declaring connection failure. */
#define ELOG_HTTP_DEFAULT_CONNECT_TIMEOUT_MILLIS 5000

/** @def By default wait for 5 seconds before declaring write failure. */
#define ELOG_HTTP_DEFAULT_WRITE_TIMEOUT_MILLIS 1000

/** @def By default wait for 5 seconds before declaring read failure. */
#define ELOG_HTTP_DEFAULT_READ_TIMEOUT_MILLIS 1000

/** @def By default wait for 5 seconds before trying to resend failed HTTP messages. */
#define ELOG_HTTP_DEFAULT_RESEND_TIMEOUT_MILLIS 5000

/** @def By default allow for a total 1 MB of payload to be backlogged for resend. */
#define ELOG_HTTP_DEFAULT_BACKLOG_SIZE_BYTES (1024 * 1024)

/**
 * @def By default wait for 5 seconds before trying to resend failed HTTP messages during
 * shutdown.
 */
#define ELOG_HTTP_DEFAULT_SHUTDOWN_TIMEOUT_MILLIS 5000

class ELOG_API ELogHttpResultHandler {
public:
    /**
     * @brief Handle HTTP send result.
     * @param result The result to examine.
     * @return true If the result is regarded as success.
     * @return false If the result is regarded as failure, in which case the HTTP message will be
     * stored for future attempt to resend to the server. Pay attention that when some errors occur
     * it does not make sense to resend, since the same error would occur again (e.g. invalid
     * payload, wrong endpoint name, etc.).
     */
    virtual bool handleResult(const httplib::Result& result) = 0;
};

/** @brief Abstract parent class for sending log data over HTTP. */
class ELOG_API ELogHttpClient {
public:
    ELogHttpClient()
        : m_connectTimeoutMillis(0),
          m_writeTimeoutMillis(0),
          m_readTimeoutMillis(0),
          m_resendPeriodMillis(0),
          m_backlogLimitBytes(0),
          m_shutdownTimeoutMillis(0),
          m_client(nullptr),
          m_resendClient(nullptr),
          m_resultHandler(nullptr),
          m_backlogSizeBytes(0),
          m_stopResend(false) {}

    ~ELogHttpClient() {}

    /**
     * @brief Initializes the HTTP client.
     *
     * @param serverAddress The HTTP server address.
     * @param resultHandler Optional result handler to help decide whether HTTP messages should be
     * resent.
     * @param connectTimeoutMillis Optionally specify the timeout before declaring connection
     * failure.
     * @param writeTimeoutMillis Optionally specify the timeout before declaring write failure.
     * @param readTimeoutMillis Optionally specify the timeout before declaring read failure.
     * @param resendPeriodMillis Optionally specify the timeout resending failed HTTP message.
     * @param backlogLimitBytes Optionally specify the maximum size in byte of the backlog.
     * @param shutdownTimeoutMillis Optionally specify the timeout for resending failed HTTP message
     * during shutdown.
     * @param headerNames Optionally specify names of headers to be sent with each request.
     * @param headerValues Optionally specify values of headers to be sent with each request.
     * @param headerCount Number of headers to be sent with each request.
     */
    void initialize(const char* serverAddress, ELogHttpResultHandler* resultHandler = nullptr,
                    uint32_t connectTimeoutMillis = ELOG_HTTP_DEFAULT_CONNECT_TIMEOUT_MILLIS,
                    uint32_t writeTimeoutMillis = ELOG_HTTP_DEFAULT_WRITE_TIMEOUT_MILLIS,
                    uint32_t readTimeoutMillis = ELOG_HTTP_DEFAULT_READ_TIMEOUT_MILLIS,
                    uint32_t resendPeriodMillis = ELOG_HTTP_DEFAULT_RESEND_TIMEOUT_MILLIS,
                    uint32_t backlogLimitBytes = ELOG_HTTP_DEFAULT_BACKLOG_SIZE_BYTES,
                    uint32_t shutdownTimeoutMillis = ELOG_HTTP_DEFAULT_SHUTDOWN_TIMEOUT_MILLIS,
                    const char** headerNames = nullptr, const char** headerValues = nullptr,
                    size_t headerCount = 0);

    /** @brief Starts the HTTP client. */
    bool start();

    /** @brief Stops the HTTP client. */
    bool stop();

    /**
     * @brief Posts HTTP message to a given endpoint (using HTTP POST).
     *
     * @param endpoint The endpoint. Expected resource path starting with forward slash.
     * @param body The message's body.
     * @param len The message's length.
     * @param contentType The message's content type.
     * @param headerNames Optional header names to be passed with the message. This string array
     * must have at least @ref headerCount valid elements.
     * @param headerValues Optional header values to be passed with the message. This string array
     * must have at least @ref headerCount valid elements.
     * @param headerCount The number of valid headers.
     * @param compress Specifies whether to compress the message (using gzip).
     * @return std::pair<bool, int> A pair denoting wether message sending was successful and if so
     * then the HTTP status returned by the server.
     * @note The HTTP result handler is invoked in order to decide whether to save the message for
     * resending. If none was installed and message sending failed, then the message si discarded
     * and will not be an attempt to resend the message to the server.
     */
    std::pair<bool, int> post(const char* endpoint, const char* body, size_t len,
                              const char* contentType = "application/json",
                              const char** headerNames = nullptr,
                              const char** headerValues = nullptr, size_t headerCount = 0,
                              bool compress = false);

private:
    std::string m_serverAddress;
    uint32_t m_connectTimeoutMillis;
    uint32_t m_writeTimeoutMillis;
    uint32_t m_readTimeoutMillis;
    uint32_t m_resendPeriodMillis;
    uint32_t m_backlogLimitBytes;
    uint32_t m_shutdownTimeoutMillis;
    httplib::Headers m_headers;

    httplib::Client* m_client;
    httplib::Client* m_resendClient;
    ELogHttpResultHandler* m_resultHandler;

    httplib::Client* createClient();

    void addBacklog(const char* endpoint, const httplib::Headers& headers, const char* body,
                    size_t len, const char* contentType);

    struct HttpMessage {
        HttpMessage(const char* endpoint, const httplib::Headers& headers, const char* body,
                    size_t len, const char* contentType);
        std::string m_endpoint;
        httplib::Headers m_headers;
        std::vector<char> m_body;
        std::string m_contentType;
    };

    std::list<HttpMessage> m_pendingBackLog;
    std::list<HttpMessage> m_shippingBackLog;
    uint32_t m_backlogSizeBytes;
    std::mutex m_lock;
    std::condition_variable m_cv;

    std::thread m_resendThread;
    bool m_stopResend;

    void resendThread();
    bool shouldStopResend();
    void stopResendThread();
    void copyPendingBacklog();
    void dropExcessBacklog();
    bool resendShippingBacklog(bool duringShutdown = false);
};

}  // namespace elog

#endif  // ELOG_ENABLE_HTTP

#endif  // __ELOG_HTTP_CLIENT_H__