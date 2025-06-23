#include "elog_datadog_target.h"

#ifdef ELOG_ENABLE_DATADOG_CONNECTOR

#include <gzip/compress.hpp>

#include "elog_common.h"
#include "elog_error.h"
#include "elog_field_selector_internal.h"
#include "elog_json_receptor.h"
#include "elog_logger.h"

#ifdef ELOG_ENABLE_STACK_TRACE
#include "elog_stack_trace.h"
#endif

namespace elog {

bool ELogDatadogTarget::startLogTarget() {
    if (!m_tags.empty() && !parseTags(m_tags)) {
        return false;
    }

    ELOG_REPORT_TRACE("Creating HTTP client to Datadog at: %s", m_endpoint.c_str());
    m_client = new httplib::Client(m_endpoint);
    ELOG_REPORT_TRACE("HTTP client created");
    if (!m_client->is_valid()) {
        ELOG_REPORT_ERROR("Connection to Datadog endpoint %s is not valid", m_endpoint.c_str());
        delete m_client;
        m_client = nullptr;
        return false;
    }

    // set timeouts
    ELOG_REPORT_TRACE("Datadog log target setting connect timeout millis to %u",
                      m_connectTimeoutMillis);
    m_client->set_connection_timeout(
        std::chrono::milliseconds(m_connectTimeoutMillis));  // micro-seconds
    m_client->set_write_timeout(std::chrono::milliseconds(m_writeTimeoutMillis));
    m_client->set_read_timeout(std::chrono::milliseconds(m_readTimeoutMillis));

    // make sure the json object is of array type
    m_logItemArray = nlohmann::json::array();

    return true;
}

bool ELogDatadogTarget::stopLogTarget() {
    if (m_client != nullptr) {
        delete m_client;
        m_client = nullptr;
    }
    return true;
}

uint32_t ELogDatadogTarget::writeLogRecord(const ELogRecord& logRecord) {
    if (m_client == nullptr) {
        return 0;
    }

    ELOG_REPORT_TRACE("Preapring log message for Datadog");
    uint32_t index = m_logItemArray.size();

    // log line
    std::string logMsg;
    formatLogMsg(logRecord, logMsg);
    m_logItemArray[index]["message"] = logMsg;

    // more metadata
    m_logItemArray[index]["status"] = elogLevelToStr(logRecord.m_logLevel);
    m_logItemArray[index]["hostname"] = getHostName();
    // TODO: this is not working well, neither as int, nor as string
    // m_logItemArray[index]["timestamp"] = elogTimeToUTCSeconds(logRecord.m_logTime);
    m_logItemArray[index]["logger.name"] = logRecord.m_logger->getLogSource()->getQualifiedName();

    const char* threadName = getCurrentThreadNameField();
    if (threadName != nullptr && *threadName != 0) {
        m_logItemArray[index]["logger.thread_name"] = threadName;
    }

    if (!m_source.empty()) {
        m_logItemArray[index]["ddsource"] = m_source.c_str();
    }
    if (!m_service.empty()) {
        m_logItemArray[index]["service"] = m_service.c_str();
    }

    if (m_stackTrace) {
#ifdef ELOG_ENABLE_STACK_TRACE
        std::string stackTrace;
        if (getStackTraceString(stackTrace)) {
            m_logItemArray[index]["error.stack"] = stackTrace;
        }
#endif
    }

    // tags
    ELogJsonReceptor receptor;
    fillInTags(logRecord, &receptor);
    std::string tags;
    if (!prepareTagsString(getTagNames(), receptor.getPropValues(), tags)) {
        ELOG_REPORT_ERROR("Failed to prepare datadog tags");
        return 0;
    }
    m_logItemArray[index]["ddtags"] = tags;
    /*if (!receptor.prepareJsonMap(m_logItemArray[index]["ddtags"], getTagNames())) {
        return 0;
    }*/

    fprintf(stderr, "Log message for Datadog is ready, body: %s\n", m_logItemArray.dump().c_str());
    return logMsg.size();
}

void ELogDatadogTarget::flushLogTarget() {
    ELOG_REPORT_TRACE("POST log message for Datadog: %s", m_logItemArray.dump().c_str());
    httplib::Headers headers;
    headers.insert(httplib::Headers::value_type("DD-API-KEY", m_apiKey));
    std::string body =
        m_logItemArray.size() == 1 ? m_logItemArray[0].dump() : m_logItemArray.dump();
    if (m_compress) {
        body = gzip::compress(body.data(), body.size(), Z_BEST_COMPRESSION);
        headers.insert(httplib::Headers::value_type("Content-Encoding", "gzip"));
        headers.insert(httplib::Headers::value_type("Content-Length", std::to_string(body.size())));
    }
    httplib::Result res =
        m_client->Post("/api/v2/logs", headers, body.data(), body.size(), "application/json");
    ELOG_REPORT_TRACE("POST done");
    if (!res) {
        ELOG_REPORT_ERROR("Failed to POST HTTP request to Datadog: %s",
                          httplib::to_string(res.error()).c_str());
    } else if (res->status != 202) {  // 202 is: Request accepted for processing
        ELOG_REPORT_ERROR("Received status %d from Datadog server, body: %s, reason: %s",
                          res->status, res->body.c_str(), res->reason.c_str());
        for (const auto& header : res->headers) {
            ELOG_REPORT_ERROR("Header: %s = %s", header.first.c_str(), header.second.c_str());
        }
    }

    // clear the log entry for next round
    m_logItemArray = nlohmann::json::array();
}

bool ELogDatadogTarget::prepareTagsString(const std::vector<std::string>& propNames,
                                          const std::vector<std::string>& propValues,
                                          std::string& tags) {
    // datadog requires that the tags be flattened to a string of a comma-separated list of
    // name:value pairs
    if (propNames.size() != propValues.size()) {
        ELOG_REPORT_ERROR(
            "Cannot prepare Datadog log target tags, property name and value count mismatch");
        return false;
    }
    std::stringstream s;
    for (uint32_t i = 0; i < propNames.size(); ++i) {
        s << propNames[i] << ":" << propValues[i];
        if (i + 1 < propNames.size()) {
            s << ",";
        }
    }
    tags = s.str();
    return true;
}

}  // namespace elog

#endif  // ELOG_ENABLE_GRAFANA_CONNECTOR