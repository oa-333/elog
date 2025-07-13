#include "elog_http_client.h"

#ifdef ELOG_ENABLE_HTTP

#include <chrono>

#include "elog_error.h"

namespace elog {

void ELogHttpClient::initialize(
    const char* serverAddress, ELogHttpResultHandler* resultHandler /* = nullptr */,
    uint32_t connectTimeoutMillis /* = ELOG_HTTP_DEFAULT_CONNECT_TIMEOUT_MILLIS */,
    uint32_t writeTimeoutMillis /* = ELOG_HTTP_DEFAULT_WRITE_TIMEOUT_MILLIS */,
    uint32_t readTimeoutMillis /* = ELOG_HTTP_DEFAULT_READ_TIMEOUT_MILLIS */,
    uint32_t resendPeriodMillis /* = ELOG_HTTP_DEFAULT_RESEND_TIMEOUT_MILLIS */,
    uint32_t backlogSizeBytes /* = ELOG_HTTP_DEFAULT_BACKLOG_SIZE_BYTES */,
    uint32_t shutdownTimeoutMillis /* = ELOG_HTTP_DEFAULT_SHUTDOWN_TIMEOUT_MILLIS */,
    const char** headerNames /* = nullptr */, const char** headerValues /* = nullptr */,
    size_t headerCount /* = 0 */) {
    // save configuration
    m_serverAddress = serverAddress;
    m_resultHandler = resultHandler;
    m_connectTimeoutMillis = connectTimeoutMillis;
    m_writeTimeoutMillis = writeTimeoutMillis;
    m_readTimeoutMillis = readTimeoutMillis;
    m_resendPeriodMillis = resendPeriodMillis;
    m_backlogLimitBytes = backlogSizeBytes;
    m_shutdownTimeoutMillis = shutdownTimeoutMillis;
    for (size_t i = 0; i < headerCount; ++i) {
        m_headers.insert(httplib::Headers::value_type(headerNames[i], headerValues[i]));
    }
}

bool ELogHttpClient::start() {
    m_client = createClient();
    if (m_client == nullptr) {
        return false;
    }
    m_resendClient = createClient();
    if (m_resendClient == nullptr) {
        delete m_client;
        m_client = nullptr;
        return false;
    }

    // start resend thread
    m_resendThread = std::thread([this] { resendThread(); });
    return true;
}

bool ELogHttpClient::stop() {
    // stop resend thread, but only if http clients are valid
    if (m_resendClient != nullptr) {
        stopResendThread();
        delete m_resendClient;
        m_resendClient = nullptr;
    }
    if (m_client != nullptr) {
        delete m_client;
        m_client = nullptr;
    }
    return true;
}

std::pair<bool, int> ELogHttpClient::post(const char* endpoint, const char* body, size_t len,
                                          const char* contentType /* = "application/json" */,
                                          const char** headerNames /* = nullptr */,
                                          const char** headerValues /* = nullptr */,
                                          size_t headerCount /* = 0 */,
                                          bool compress /* = false */) {
    // start with default headers specified in initialize(), and add additional headers if any
    ELOG_REPORT_TRACE("POST log data to HTTP address/endpoint: %s/%s", m_serverAddress.c_str(),
                      endpoint);
    httplib::Headers headers = m_headers;
    for (size_t i = 0; i < headerCount; ++i) {
        headers.insert(httplib::Headers::value_type(headerNames[i], headerValues[i]));
    }

    // compress body if needed
    std::string compressedBody;
    if (compress) {
        compressedBody = gzip::compress(body, len, Z_BEST_COMPRESSION);
        headers.insert(httplib::Headers::value_type("Content-Encoding", "gzip"));
        headers.insert(
            httplib::Headers::value_type("Content-Length", std::to_string(compressedBody.size())));
        ELOG_REPORT_TRACE("Compressed HTTP log data from %zu to %zu", len, compressedBody.size());
        body = compressedBody.c_str();
        len = compressedBody.size();
    }

    // POST HTTP message
    ELOG_REPORT_TRACE("Sending data to HTTP server %s/%s via POST", m_serverAddress.c_str(),
                      endpoint);
    httplib::Result res = m_client->Post(endpoint, headers, body, len, contentType);
    ELOG_REPORT_TRACE("POST done");
    if (!res) {
        ELOG_REPORT_ERROR("Failed to POST HTTP request: %s",
                          httplib::to_string(res.error()).c_str());
        // no need to consult result handler, this is a clear network error
        addBacklog(endpoint, headers, body, len, contentType);
        return {false, -1};  // no status when result evaluates to false
    }

    // consult with result to handler so we can tell whether resend is required
    ELOG_REPORT_TRACE("HTTP status: %d", res->status);
    if (m_resultHandler != nullptr && !m_resultHandler->handleResult(res)) {
        addBacklog(endpoint, headers, body, len, contentType);
    }
    return {true, res->status};
}

httplib::Client* ELogHttpClient::createClient() {
    ELOG_REPORT_TRACE("Creating HTTP client to server at: %s", m_serverAddress.c_str());
    httplib::Client* client = new httplib::Client(m_serverAddress);
    ELOG_REPORT_TRACE("HTTP client created");
    if (!client->is_valid()) {
        ELOG_REPORT_ERROR("HTTP connection to server %s is not valid", m_serverAddress.c_str());
        delete client;
        return nullptr;
    }

    // set connection timeouts
    client->set_connection_timeout(
        std::chrono::milliseconds(m_connectTimeoutMillis));  // micro-seconds
    client->set_write_timeout(std::chrono::milliseconds(m_writeTimeoutMillis));
    client->set_read_timeout(std::chrono::milliseconds(m_readTimeoutMillis));

    return client;
}

void ELogHttpClient::addBacklog(const char* endpoint, const httplib::Headers& headers,
                                const char* body, size_t len, const char* contentType) {
    std::unique_lock<std::mutex> lock(m_lock);
    m_pendingBackLog.emplace_back(endpoint, headers, body, len, contentType);
    m_cv.notify_one();
}

void ELogHttpClient::resendThread() {
    while (!shouldStopResend()) {
        // wait the full period until ordered to stop or that we are urged to resend
        {
            std::unique_lock<std::mutex> lock(m_lock);
            m_cv.wait_for(lock, std::chrono::milliseconds(m_resendPeriodMillis),
                          [this] { return m_stopResend || !m_pendingBackLog.empty(); });
            if (m_stopResend) {
                break;
            }

            // get out all pending back log messages and put in shipping back log queue, so we can
            // release the lock quickly for more pending messages, otherwise we would be holding the
            // lock while sending HTTP messages from the back log queue
            copyPendingBacklog();
        }

        // see if we exceeded limit
        dropExcessBacklog();

        // now retry to send queued back log messages
        resendShippingBacklog();
    }

    // one last attempt before shutdown
    if (m_shutdownTimeoutMillis > 0) {
        // copy newly added pending blacklog into shipping blacklog (one last time)
        copyPendingBacklog();

        // compute a reasonable sleep time between resend attempt
        size_t backlogCount = m_shippingBackLog.size();
        if (backlogCount == 0) {
            // nothing to send
            return;
        }
        uint32_t resendPeriodMillis = m_shutdownTimeoutMillis / (backlogCount + 1);

        // attempt resending failed messages
        auto start = std::chrono::high_resolution_clock::now();
        std::chrono::milliseconds timePassedMillis;
        do {
            // try to resend backlog
            if (resendShippingBacklog(true)) {
                // that's it, all pending messages were sent
                break;
            }

            // otherwise sleep a bit before next round
            std::this_thread::sleep_for(std::chrono::milliseconds(resendPeriodMillis));

            // compute time passed
            auto end = std::chrono::high_resolution_clock::now();
            timePassedMillis = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        } while (timePassedMillis.count() <= m_shutdownTimeoutMillis);
    }
}

void ELogHttpClient::copyPendingBacklog() {
    while (!m_pendingBackLog.empty()) {
        m_backlogSizeBytes += (uint32_t)m_pendingBackLog.front().m_body.size();
        m_shippingBackLog.push_back(m_pendingBackLog.front());
        m_pendingBackLog.pop_front();
    }
}

void ELogHttpClient::dropExcessBacklog() {
    while (m_backlogSizeBytes >= m_backlogLimitBytes) {
        if (m_shippingBackLog.empty()) {
            // impossible, but we still must not generate core dump
            ELOG_REPORT_ERROR(
                "Invalid resend thread state: backlog size (%u bytes) exceeds limit, but "
                "backlog is empty",
                m_backlogSizeBytes);
            break;
        }
        size_t messageSize = m_shippingBackLog.front().m_body.size();
        if (messageSize > m_backlogSizeBytes) {
            // impossible, but still be careful
            ELOG_REPORT_ERROR(
                "Message body size (%zu bytes) exceeds current backlog size (%u bytes), "
                "resetting backlog size to zero",
                messageSize, m_backlogSizeBytes);
            m_backlogLimitBytes = 0;
        } else {
            m_backlogSizeBytes -= (uint32_t)messageSize;
        }
        m_shippingBackLog.pop_front();
    }
}

bool ELogHttpClient::resendShippingBacklog(bool duringShutdown /* = false */) {
    ELOG_REPORT_TRACE("Attempting to resend %zu HTTP pending messages", m_shippingBackLog.size());
    while (!m_shippingBackLog.empty() && (duringShutdown || !shouldStopResend())) {
        HttpMessage& msg = m_shippingBackLog.front();
        httplib::Result res = m_resendClient->Post(msg.m_endpoint, msg.m_headers, &msg.m_body[0],
                                                   msg.m_body.size(), msg.m_contentType);
        ELOG_REPORT_TRACE("POST done");
        if (!res) {
            ELOG_REPORT_ERROR("Failed to resend POST HTTP request: %s",
                              httplib::to_string(res.error()).c_str());
            // no need to consult result handler, this is a clear network error
            return false;
        }
        if (m_resultHandler != nullptr && !m_resultHandler->handleResult(res)) {
            return false;
        }
        m_shippingBackLog.pop_front();
    }
    return true;
}

bool ELogHttpClient::shouldStopResend() {
    std::unique_lock<std::mutex> lock(m_lock);
    return m_stopResend;
}

void ELogHttpClient::stopResendThread() {
    {
        std::unique_lock<std::mutex> lock(m_lock);
        m_stopResend = true;
        m_cv.notify_one();
    }
    m_resendThread.join();
}

ELogHttpClient::HttpMessage::HttpMessage(const char* endpoint, const httplib::Headers& headers,
                                         const char* body, size_t len, const char* contentType)
    : m_endpoint(endpoint), m_headers(headers), m_contentType(contentType) {
    m_body.assign(body, body + len);
}

}  // namespace elog

#endif  // ELOG_ENABLE_HTTP