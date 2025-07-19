#include "elog_datadog_target.h"

#ifdef ELOG_ENABLE_DATADOG_CONNECTOR

#include "elog_common.h"
#include "elog_error.h"
#include "elog_field_selector_internal.h"
#include "elog_json_receptor.h"
#include "elog_logger.h"

#ifdef ELOG_ENABLE_STACK_TRACE
#include "elog_stack_trace.h"
#endif

namespace elog {

static const int ELOG_DATADOG_HTTP_SUCCESS_STATUS = 204;

ELogDatadogTarget::ELogDatadogTarget(const char* serverAddress, const char* apiKey,
                                     const ELogHttpConfig& config, const char* source /* = "" */,
                                     const char* service /* = "" */, const char* tags /* = "" */,
                                     bool stackTrace /* = false */, bool compress /* = false */)
    : ELogHttpClientAssistant("Datadog", ELOG_DATADOG_HTTP_SUCCESS_STATUS),
      m_apiKey(apiKey),
      m_source(source),
      m_service(service),
      m_tags(tags),
      m_stackTrace(stackTrace),
      m_compress(compress) {
    ELOG_REPORT_TRACE("Creating HTTP client to Datadog at: %s", serverAddress);
    m_client.initialize(serverAddress, "Datadog", config, this);
}

void ELogDatadogTarget::embedHeaders(httplib::Headers& headers) {
    headers.insert(httplib::Headers::value_type("DD-API-KEY", m_apiKey));
}

bool ELogDatadogTarget::startLogTarget() {
    if (!m_tags.empty() && !parseTags(m_tags)) {
        return false;
    }

    // make sure the json object is of array type
    m_logItemArray = nlohmann::json::array();

    return m_client.start();
}

bool ELogDatadogTarget::stopLogTarget() { return m_client.stop(); }

uint32_t ELogDatadogTarget::writeLogRecord(const ELogRecord& logRecord) {
    ELOG_REPORT_TRACE("Preapring log message for Datadog");
    size_t index = m_logItemArray.size();

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

    ELOG_REPORT_TRACE("Log message for Datadog is ready, body: %s", m_logItemArray.dump().c_str());
    return (uint32_t)logMsg.size();
}

void ELogDatadogTarget::flushLogTarget() {
    std::string body =
        m_logItemArray.size() == 1 ? m_logItemArray[0].dump() : m_logItemArray.dump();
    ELOG_REPORT_TRACE("POST log message for Datadog: %s", body.c_str());
    m_client.post("/api/v2/logs", body.data(), body.size(), "application/json", m_compress);

    // clear the log entry for next round
    // NOTE: if resend needs to take place, then the body has already been copied tp the backlog)
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

#endif  // ELOG_ENABLE_DATADOG_CONNECTOR