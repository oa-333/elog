#include "elog_grafana_json_target.h"

#ifdef ELOG_ENABLE_GRAFANA_CONNECTOR

#include "elog_common.h"
#include "elog_json_receptor.h"
#include "elog_report.h"

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
    size_t logLineCount = values.size();
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

    // NOTE: log data is being aggregated until flush, which sends HTTP message to server

    ELOG_REPORT_TRACE("Log message for Grafana Loki is ready");
    return (uint32_t)logMsg.size();
}

void ELogGrafanaJsonTarget::flushLogTarget() {
    std::string jsonBody = m_logEntry.dump();
    ELOG_REPORT_TRACE("POST log message for Grafana Loki: %s", jsonBody.c_str());
    m_client.post("/loki/api/v1/push", jsonBody.data(), jsonBody.size(), "application/json");
    // clear the log entry for next round
    // NOTE: if resend needs to take place, then the body has already been copied tp the backlog)
    m_logEntry.clear();
}

}  // namespace elog

#endif  // ELOG_ENABLE_GRAFANA_CONNECTOR