#ifndef __ELOG_HTTP_CLIENT_H__
#define __ELOG_HTTP_CLIENT_H__

#ifdef ELOG_ENABLE_HTTP

#include <httplib.h>

#include "elog_def.h"
#include "elog_gzip.h"
#include "elog_http_config.h"

/** @def HTTP status OK. */
#define ELOG_HTTP_STATUS_OK 200

/** @def HTTP status ACCEPTED (asynchronous processing). */
#define ELOG_HTTP_STATUS_ACCEPTED 202

/** @def HTTP status NO CONTENT (result OK, no specific response data). */
#define ELOG_HTTP_STATUS_NO_CONTENT 204

namespace elog {

/** @brief An assistant to carry out HTTP client operations. */
class ELOG_API ELogHttpClientAssistant {
public:
    virtual ~ELogHttpClientAssistant() {}
    ELogHttpClientAssistant(const ELogHttpClientAssistant&) = delete;
    ELogHttpClientAssistant(ELogHttpClientAssistant&&) = delete;
    ELogHttpClientAssistant& operator=(const ELogHttpClientAssistant&) = delete;

    /** @brief Embed headers in outgoing HTTP message. */
    virtual void embedHeaders(httplib::Headers& headers) {}

    /**
     * @brief Handles HTTP POST result.
     * @param result The result to examine.
     * @return true If the result is regarded as success.
     * @return false If the result is regarded as failure, in which case the HTTP message will be
     * stored in a backlog for future attempt to resend to the server. Pay attention that when some
     * errors occur it does not make sense to resend, since the same error would occur again (e.g.
     * invalid payload, wrong endpoint name, etc.).
     */
    virtual bool handleResult(const httplib::Result& result);

protected:
    /**
     * @brief Construct a new assistant object.
     *
     * @param logTargetName The log target name (for error reporting purposes).
     * @param status Optionally specify the expected response status. By default it is HTTP 200 OK.
     */
    ELogHttpClientAssistant(const char* logTargetName, int status = ELOG_HTTP_STATUS_OK)
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
          m_disableResend(false),
          m_backlogSizeBytes(0),
          m_stopResend(false) {}

    ELogHttpClient(const ELogHttpClient&) = delete;
    ELogHttpClient(ELogHttpClient&&) = delete;
    ELogHttpClient& operator=(const ELogHttpClient&) = delete;
    ~ELogHttpClient() {}

    /**
     * @brief Initializes the HTTP client.
     *
     * @param serverAddress The HTTP server address.
     * @param serverName The server name (for logging purposes).
     * @param httpConfig Timeouts and backlog configuration
     * @param assistant Optional assistant in carrying out client operations.
     * @param disableResend Optionally disable resend task.
     */
    void initialize(const char* serverAddress, const char* serverName,
                    const ELogHttpConfig& httpConfig, ELogHttpClientAssistant* assistant = nullptr,
                    bool disableResend = false);

    /** @brief Starts the HTTP client. */
    bool start();

    /** @brief Stops the HTTP client. */
    bool stop();

    /**
     * @brief Sends HTTP message to a given endpoint (using HTTP POST).
     *
     * @param endpoint The endpoint. Expected resource path starting with forward slash.
     * @param body The message's body.
     * @param len The message's length.
     * @param contentType The message's content type.
     * @param compress Specifies whether to compress the message (using gzip).
     * @param responseBody Optionally receives the response body.
     * @return std::pair<bool, int> A pair denoting wether message sending was successful and if so
     * then the HTTP status returned by the server.
     * @note The HTTP result handler is invoked in order to decide whether to save the message for
     * resending. If none was installed and message sending failed, then the message si discarded
     * and will not be an attempt to resend the message to the server.
     */
    std::pair<bool, int> post(const char* endpoint, const char* body, size_t len,
                              const char* contentType = "application/json", bool compress = false,
                              std::string* responseBody = nullptr);

    /**
     * @brief Sends HTTP message to a given endpoint (using HTTP PUT).
     *
     * @param endpoint The endpoint. Expected resource path starting with forward slash.
     * @param body The message's body.
     * @param len The message's length.
     * @param contentType The message's content type.
     * @param compress Specifies whether to compress the message (using gzip).
     * @param responseBody Optionally receives the response body.
     * @return std::pair<bool, int> A pair denoting wether message sending was successful and if so
     * then the HTTP status returned by the server.
     * @note The HTTP result handler is invoked in order to decide whether to save the message for
     * resending. If none was installed and message sending failed, then the message si discarded
     * and will not be an attempt to resend the message to the server.
     */
    std::pair<bool, int> put(const char* endpoint, const char* body, size_t len,
                             const char* contentType = "application/json", bool compress = false,
                             std::string* responseBody = nullptr);

    /**
     * @brief Sends HTTP message to a given endpoint (using HTTP GET).
     *
     * @param endpoint The endpoint. Expected resource path starting with forward slash.
     * @param body The message's body.
     * @param len The message's length.
     * @param contentType The message's content type.
     * @param compress Specifies whether to compress the message (using gzip).
     * @param responseBody Optionally receives the response body.
     * @return std::pair<bool, int> A pair denoting wether message sending was successful and if so
     * then the HTTP status returned by the server.
     * @note The HTTP result handler is invoked in order to decide whether to save the message for
     * resending. If none was installed and message sending failed, then the message si discarded
     * and will not be an attempt to resend the message to the server.
     */
    std::pair<bool, int> get(const char* endpoint, const char* body, size_t len,
                             const char* contentType = "application/json", bool compress = false,
                             std::string* responseBody = nullptr);

    /**
     * @brief Sends HTTP message to a given endpoint (using HTTP DELETE).
     *
     * @param endpoint The endpoint. Expected resource path starting with forward slash.
     * @param body The message's body.
     * @param len The message's length.
     * @param contentType The message's content type.
     * @param compress Specifies whether to compress the message (using gzip).
     * @param responseBody Optionally receives the response body.
     * @return std::pair<bool, int> A pair denoting wether message sending was successful and if so
     * then the HTTP status returned by the server.
     * @note The HTTP result handler is invoked in order to decide whether to save the message for
     * resending. If none was installed and message sending failed, then the message si discarded
     * and will not be an attempt to resend the message to the server.
     */
    std::pair<bool, int> del(const char* endpoint, const char* body, size_t len,
                             const char* contentType = "application/json", bool compress = false,
                             std::string* responseBody = nullptr);

private:
    std::string m_serverAddress;
    std::string m_logTargetName;
    ELogHttpConfig m_config;
    httplib::Client* m_client;
    httplib::Client* m_resendClient;
    ELogHttpClientAssistant* m_assistant;
    bool m_disableResend;

    enum HttpMethod { HM_POST, HM_PUT, HM_GET, HM_DEL };

    std::pair<bool, int> sendHttpMsg(HttpMethod method, const char* endpoint, const char* body,
                                     size_t len, const char* contentType = "application/json",
                                     bool compress = false, std::string* responseBody = nullptr);

    const char* getMethodName(HttpMethod method);

    httplib::Result execHttpRequest(HttpMethod method, const char* endpoint,
                                    const httplib::Headers& headers, const char* body, size_t len,
                                    const char* contentType);

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
    uint64_t m_backlogSizeBytes;
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