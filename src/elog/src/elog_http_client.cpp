#include "elog_http_client.h"

#include "elog_field_selector_internal.h"

#ifdef ELOG_ENABLE_HTTP

#include <cassert>
#include <chrono>
#include <cinttypes>

#include "elog_report.h"

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogHttpClient)

bool ELogHttpClientAssistant::handleResult(const httplib::Result& result) {
    if (result->status != m_status) {
        ELOG_REPORT_ERROR(
            "Received error status %d from %s server (expecting %d), body: %s, reason: %s",
            result->status, m_logTargetName.c_str(), m_status, result->body.c_str(),
            result->reason.c_str());
        for (const auto& header : result->headers) {
            ELOG_REPORT_ERROR("Header: %s = %s", header.first.c_str(), header.second.c_str());
        }
        return false;
    }
    return true;
}

void ELogHttpClient::initialize(const char* serverAddress, const char* logTargetName,
                                const ELogHttpConfig& httpConfig,
                                ELogHttpClientAssistant* assistant /* = nullptr */,
                                bool disableResend /* = false */) {
    // save configuration
    m_serverAddress = serverAddress;
    m_logTargetName = logTargetName;
    m_config = httpConfig;
    m_assistant = assistant;
    m_disableResend = disableResend;
}

bool ELogHttpClient::start() {
    m_client = createClient();
    if (m_client == nullptr) {
        return false;
    }
    if (!m_disableResend) {
        m_resendClient = createClient();
        if (m_resendClient == nullptr) {
            delete m_client;
            m_client = nullptr;
            return false;
        }

        // start resend thread
        m_resendThread = std::thread([this] {
            setCurrentThreadNameField("http-resend");
            resendThread();
        });
    }
    return true;
}

bool ELogHttpClient::stop() {
    // stop resend thread, but only if http clients are valid
    if (!m_disableResend) {
        if (m_resendClient != nullptr) {
            stopResendThread();
            delete m_resendClient;
            m_resendClient = nullptr;
        }
    }
    if (m_client != nullptr) {
        delete m_client;
        m_client = nullptr;
    }
    return true;
}

std::pair<bool, int> ELogHttpClient::post(const char* endpoint, const char* body, size_t len,
                                          const char* contentType /* = "application/json" */,
                                          bool compress /* = false */,
                                          std::string* responseBody /* = nullptr */) {
    return sendHttpMsg(HM_POST, endpoint, body, len, contentType, compress, responseBody);
}

std::pair<bool, int> ELogHttpClient::put(const char* endpoint, const char* body, size_t len,
                                         const char* contentType /* = "application/json" */,
                                         bool compress /* = false */,
                                         std::string* responseBody /* = nullptr */) {
    return sendHttpMsg(HM_PUT, endpoint, body, len, contentType, compress, responseBody);
}

std::pair<bool, int> ELogHttpClient::get(const char* endpoint, const char* body, size_t len,
                                         const char* contentType /* = "application/json" */,
                                         bool compress /* = false */,
                                         std::string* responseBody /* = nullptr */) {
    return sendHttpMsg(HM_GET, endpoint, body, len, contentType, compress, responseBody);
}

std::pair<bool, int> ELogHttpClient::del(const char* endpoint, const char* body, size_t len,
                                         const char* contentType /* = "application/json" */,
                                         bool compress /* = false */,
                                         std::string* responseBody /* = nullptr */) {
    return sendHttpMsg(HM_DEL, endpoint, body, len, contentType, compress, responseBody);
}

std::pair<bool, int> ELogHttpClient::sendHttpMsg(HttpMethod method, const char* endpoint,
                                                 const char* body, size_t len,
                                                 const char* contentType /* = "application/json" */,
                                                 bool compress /* = false */,
                                                 std::string* responseBody /* = nullptr */) {
    // start with default headers specified in initialize(), and add additional headers if any
    const char* methodName = getMethodName(method);
    ELOG_REPORT_TRACE("%s log data to %s at HTTP address/endpoint: %s/%s", methodName,
                      m_logTargetName.c_str(), m_serverAddress.c_str(), endpoint);
    httplib::Headers headers;
    if (m_assistant != nullptr) {
        m_assistant->embedHeaders(headers);
    }

    // compress body if needed
    std::string compressedBody;
    if (compress) {
        compressedBody = gzip::compress(body, len, Z_BEST_COMPRESSION);
        headers.insert(httplib::Headers::value_type("Content-Encoding", "gzip"));
        headers.insert(
            httplib::Headers::value_type("Content-Length", std::to_string(compressedBody.size())));
        ELOG_REPORT_TRACE("Compressed %s HTTP log data from %zu to %zu", m_logTargetName.c_str(),
                          len, compressedBody.size());
        body = compressedBody.c_str();
        len = compressedBody.size();
    }

    // POST HTTP message
    ELOG_REPORT_TRACE("Sending data to %s at HTTP server %s/%s via %s", m_logTargetName.c_str(),
                      m_serverAddress.c_str(), endpoint, methodName);
    httplib::Result res = execHttpRequest(method, endpoint, headers, body, len, contentType);
    ELOG_REPORT_TRACE("%s done", methodName);
    if (!res) {
        ELOG_REPORT_MODERATE_ERROR_DEFAULT("Failed to %s HTTP request to %s: %s", methodName,
                                           m_logTargetName.c_str(),
                                           httplib::to_string(res.error()).c_str());
        // no need to consult result handler, this is a clear network error
        if (!m_disableResend) {
            addBacklog(endpoint, headers, body, len, contentType);
        }
        return {false, -1};  // no status when result evaluates to false
    }

    // collect response if required
    if (responseBody != nullptr) {
        *responseBody = res->body;
    }

    // consult with result to handler so we can tell whether resend is required
    ELOG_REPORT_TRACE("%s server returned HTTP status: %d", m_logTargetName.c_str(), res->status);
    if (m_assistant != nullptr && !m_assistant->handleResult(res)) {
        if (!m_disableResend) {
            addBacklog(endpoint, headers, body, len, contentType);
        }
        return {false, res->status};
    }
    return {true, res->status};
}

const char* ELogHttpClient::getMethodName(HttpMethod method) {
    switch (method) {
        case HM_POST:
            return "POST";
        case HM_PUT:
            return "PUT";
        case HM_GET:
            return "GET";
        case HM_DEL:
            return "DEL";
        default:
            return "N/A";
    }
}

httplib::Result ELogHttpClient::execHttpRequest(HttpMethod method, const char* endpoint,
                                                const httplib::Headers& headers, const char* body,
                                                size_t len, const char* contentType) {
    switch (method) {
        case HM_POST:
            return m_client->Post(endpoint, headers, body, len, contentType);
        case HM_PUT:
            return m_client->Put(endpoint, headers, body, len, contentType);
        case HM_GET:
            return m_client->Get(endpoint, headers);
        case HM_DEL:
            return m_client->Delete(endpoint, headers, body, len, contentType);
        default:
            // empty result yields unspecified error
            return httplib::Result();
    }
}

httplib::Client* ELogHttpClient::createClient() {
    ELOG_REPORT_TRACE("Creating HTTP client to %s server at: %s", m_logTargetName.c_str(),
                      m_serverAddress.c_str());
    httplib::Client* client = new httplib::Client(m_serverAddress);
    ELOG_REPORT_TRACE("%s HTTP client created", m_logTargetName.c_str());
    if (!client->is_valid()) {
        ELOG_REPORT_ERROR("HTTP connection to %s server at %s is not valid",
                          m_logTargetName.c_str(), m_serverAddress.c_str());
        delete client;
        return nullptr;
    }

    // set connection timeouts
    client->set_connection_timeout(std::chrono::milliseconds(m_config.m_connectTimeoutMillis));
    client->set_write_timeout(std::chrono::milliseconds(m_config.m_writeTimeoutMillis));
    client->set_read_timeout(std::chrono::milliseconds(m_config.m_readTimeoutMillis));

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
            m_cv.wait_for(lock, std::chrono::milliseconds(m_config.m_resendPeriodMillis),
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
    if (m_config.m_shutdownTimeoutMillis > 0) {
        // copy newly added pending blacklog into shipping blacklog (one last time)
        copyPendingBacklog();

        // compute a reasonable sleep time between resend attempt
        // NOTE: due to hard limit we know we can convert from size_t to uint32_t
        uint32_t backlogCount = (uint32_t)m_shippingBackLog.size();
        if (backlogCount == 0) {
            // nothing to send
            return;
        }
        uint64_t resendPeriodMillis = m_config.m_shutdownTimeoutMillis / (backlogCount + 1);

        // attempt resending failed messages
        // NOTE: theoretically, high resolution clock CAN go backwards, resulting in negative time
        // diff, so we use instead steady clock here, which is guaranteed to be monotonic
        auto start = std::chrono::steady_clock::now();
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
            auto end = std::chrono::steady_clock::now();
            timePassedMillis = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
            // NOTE: due to usage of steady clock, time diff cannot be negative
            assert(timePassedMillis.count() > 0);
        } while ((uint64_t)timePassedMillis.count() <= m_config.m_shutdownTimeoutMillis);
    }

    size_t backlogCount = m_shippingBackLog.size();
    if (backlogCount > 0) {
        ELOG_REPORT_ERROR("%s log target has failed to resend %zu pending messages",
                          m_logTargetName.c_str(), backlogCount);
    }
}

void ELogHttpClient::copyPendingBacklog() {
    while (!m_pendingBackLog.empty()) {
        m_backlogSizeBytes += m_pendingBackLog.front().m_body.size();
        m_shippingBackLog.push_back(m_pendingBackLog.front());
        m_pendingBackLog.pop_front();
    }
}

void ELogHttpClient::dropExcessBacklog() {
    while (m_backlogSizeBytes >= m_config.m_backlogLimitBytes) {
        if (m_shippingBackLog.empty()) {
            // impossible, but we still must not generate core dump
            ELOG_REPORT_ERROR("Invalid resend thread state: backlog size (%" PRIu64
                              " bytes) exceeds limit, but backlog is empty",
                              m_backlogSizeBytes);
            break;
        }
        size_t messageSize = m_shippingBackLog.front().m_body.size();
        if (messageSize > m_backlogSizeBytes) {
            // impossible, but still be careful
            ELOG_REPORT_ERROR("Message body size (%zu bytes) exceeds current backlog size (%" PRIu64
                              " bytes), resetting backlog size to zero",
                              messageSize, m_backlogSizeBytes);
            m_config.m_backlogLimitBytes = 0;
        } else {
            m_backlogSizeBytes -= messageSize;
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
            ELOG_REPORT_MODERATE_ERROR_DEFAULT("Failed to resend POST HTTP request: %s",
                                               httplib::to_string(res.error()).c_str());
            // no need to consult result handler, this is a clear network error
            return false;
        }
        if (m_assistant != nullptr && !m_assistant->handleResult(res)) {
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