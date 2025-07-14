#ifndef __ELOG_HTTP_CLIENT_H__
#define __ELOG_HTTP_CLIENT_H__

#define ELOG_ENABLE_HTTP
#ifdef ELOG_ENABLE_HTTP

#include <httplib.h>

#include <gzip/compress.hpp>

#include "elog_def.h"
#include "elog_http_config.h"

namespace elog {

/** @brief An assistant to carry out HTTP client operations. */
class ELOG_API ELogHttpClientAssistant {
public:
    virtual ~ELogHttpClientAssistant() {}

    /** @brief Embed headers in outgoing HTTP message. */
    virtual void embedHeaders(httplib::Headers& headers) {}

    /**
     * @brief Handles HTTP POS result.
     * @param result The result to examine.
     * @return true If the result is regarded as success.
     * @return false If the result is regarded as failure, in which case the HTTP message will be
     * stored in a backlog for future attempt to resend to the server. Pay attention that when some
     * errors occur it does not make sense to resend, since the same error would occur again (e.g.
     * invalid payload, wrong endpoint name, etc.).
     */
    bool handleResult(const httplib::Result& result);

protected:
    /**
     * @brief Construct a new assistant object.
     *
     * @param logTargetName The log target name (for error reporting purposes).
     * @param status Optionally specify the expected response status. By default it is HTTP 200 OK.
     */
    ELogHttpClientAssistant(const char* logTargetName, int status = 200)
        : m_logTargetName(logTargetName), m_status(status) {}

private:
    std::string m_logTargetName;
    int m_status;
};

/** @brief Abstract parent class for sending log data over HTTP. */
class ELOG_API ELogHttpClient {
public:
    ELogHttpClient()
        : m_client(nullptr),
          m_resendClient(nullptr),
          m_assistant(nullptr),
          m_backlogSizeBytes(0),
          m_stopResend(false) {}

    ~ELogHttpClient() {}

    /**
     * @brief Initializes the HTTP client.
     *
     * @param serverAddress The HTTP server address.
     * @param logTargetName The log target name (for logging purposes).
     * @param httpConfig Timeouts and backlog configuration
     * @param assistant Optional assistant in carrying out client operations.
     */
    void initialize(const char* serverAddress, const char* serverName,
                    const ELogHttpConfig& httpConfig, ELogHttpClientAssistant* assistant = nullptr);

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
     * @param compress Specifies whether to compress the message (using gzip).
     * @return std::pair<bool, int> A pair denoting wether message sending was successful and if so
     * then the HTTP status returned by the server.
     * @note The HTTP result handler is invoked in order to decide whether to save the message for
     * resending. If none was installed and message sending failed, then the message si discarded
     * and will not be an attempt to resend the message to the server.
     */
    std::pair<bool, int> post(const char* endpoint, const char* body, size_t len,
                              const char* contentType = "application/json", bool compress = false);

private:
    std::string m_serverAddress;
    std::string m_logTargetName;
    ELogHttpConfig m_config;
    httplib::Client* m_client;
    httplib::Client* m_resendClient;
    ELogHttpClientAssistant* m_assistant;

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