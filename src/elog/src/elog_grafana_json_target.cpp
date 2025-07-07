#include "elog_grafana_json_target.h"

#ifdef ELOG_ENABLE_GRAFANA_CONNECTOR

#include "elog_common.h"
#include "elog_error.h"
#include "elog_json_receptor.h"

namespace elog {

bool ELogGrafanaJsonTarget::startLogTarget() {
    if (!m_labels.empty() && !parseLabels(m_labels)) {
        return false;
    }
    if (!m_logLineMetadata.empty() && !parseMetadata(m_logLineMetadata)) {
        return false;
    }
    return ELogGrafanaTarget::startLogTarget();
}

uint32_t ELogGrafanaJsonTarget::writeLogRecord(const ELogRecord& logRecord) {
    if (m_client == nullptr) {
        return 0;
    }

    ELOG_REPORT_TRACE("Preapring log message for Grafana Loki");
    if (m_logEntry.empty()) {
        // apply labels (once per batch)
        ELogJsonReceptor receptor;
        fillInLabels(logRecord, &receptor);
        if (!receptor.prepareJsonMap(m_logEntry["streams"][0]["stream"], getLabelNames())) {
            return 0;
        }
    }

    // log line time, common to all log lines until flush
    auto& values = m_logEntry["streams"][0]["values"];
    uint32_t logLineCount = values.size();
    auto& logLine = values[logLineCount];
    // need to send local time, other Loki complains that timestamp is too new
    logLine[0] = std::to_string(
        std::chrono::nanoseconds(elogTimeToUTCNanos(logRecord.m_logTime, true)).count());

    // log line
    std::string logMsg;
    formatLogMsg(logRecord, logMsg);
    logLine[1] = logMsg;

    // fill int log line attributes
    if (m_metadataFormatter.getPropCount() > 0) {
        ELogJsonReceptor receptor;
        fillInMetadata(logRecord, &receptor);
        if (!receptor.prepareJsonMap(logLine[2], getMetadataNames())) {
            return 0;
        }
    }

    ELOG_REPORT_TRACE("Log message for Grafana Loki is ready");
    return logMsg.size();
}

void ELogGrafanaJsonTarget::flushLogTarget() {
    ELOG_REPORT_TRACE("POST log message for Grafana Loki: %s", m_logEntry.dump().c_str());
    httplib::Result res =
        m_client->Post("/loki/api/v1/push", m_logEntry.dump(), "application/json");
    ELOG_REPORT_TRACE("POST done");
    if (!res) {
        ELOG_REPORT_ERROR("Failed to POST HTTP request to Grafana Loki: %s",
                          httplib::to_string(res.error()).c_str());
    } else if (res->status != 204) {  // 204 is: request processed, no server content
        ELOG_REPORT_ERROR("Received status %d from Grafana server", res->status);
    }

    // clear the log entry for next round
    m_logEntry.clear();
}

}  // namespace elog

#endif  // ELOG_ENABLE_GRAFANA_CONNECTOR