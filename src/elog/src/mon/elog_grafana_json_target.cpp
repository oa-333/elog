#include "mon/elog_grafana_json_target.h"

#ifdef ELOG_ENABLE_GRAFANA_CONNECTOR

#include "elog_common.h"
#include "elog_json_receptor.h"
#include "elog_report.h"

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogGrafanaJsonTarget)

ELOG_IMPLEMENT_LOG_TARGET(ELogGrafanaJsonTarget)

bool ELogGrafanaJsonTarget::startLogTarget() {
    m_labelFormatter = ELogPropsFormatter::create();
    if (m_labelFormatter == nullptr) {
        ELOG_REPORT_ERROR("Failed to create labels properties formatter, out of memory");
        return false;
    }
    m_metadataFormatter = ELogPropsFormatter::create();
    if (m_metadataFormatter == nullptr) {
        ELOG_REPORT_ERROR("Failed to create meta-data properties formatter, out of memory");
        ELogPropsFormatter::destroy(m_labelFormatter);
        m_labelFormatter = nullptr;
        return false;
    }

    bool res = parseLabels(m_labels);
    if (res) {
        res = parseMetadata(m_logLineMetadata);
    }
    if (res) {
        res = ELogGrafanaTarget::startLogTarget();
    }

    if (!res) {
        ELogPropsFormatter::destroy(m_labelFormatter);
        ELogPropsFormatter::destroy(m_metadataFormatter);
        m_labelFormatter = nullptr;
        m_metadataFormatter = nullptr;
    }
    return res;
}

bool ELogGrafanaJsonTarget::stopLogTarget() {
    if (m_labelFormatter != nullptr) {
        ELogPropsFormatter::destroy(m_labelFormatter);
        m_labelFormatter = nullptr;
    }
    if (m_metadataFormatter != nullptr) {
        ELogPropsFormatter::destroy(m_metadataFormatter);
        m_metadataFormatter = nullptr;
    }
    return ELogGrafanaTarget::stopLogTarget();
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
        std::chrono::nanoseconds(elogTimeToUnixTimeNanos(logRecord.m_logTime, true)).count());

    // log line
    std::string logMsg;
    formatLogMsg(logRecord, logMsg);
    logLine[1] = logMsg;

    // fill int log line attributes
    if (m_metadataFormatter->getPropCount() > 0) {
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

bool ELogGrafanaJsonTarget::flushLogTarget() {
    std::string jsonBody = m_logEntry.dump();
    ELOG_REPORT_TRACE("POST log message for Grafana Loki: %s", jsonBody.c_str());
    bool res =
        m_client.post("/loki/api/v1/push", jsonBody.data(), jsonBody.size(), "application/json")
            .first;

    // clear the log entry for next round
    // NOTE: if resend needs to take place, then the body has already been copied tp the backlog)
    m_logEntry.clear();
    return res;
}

}  // namespace elog

#endif  // ELOG_ENABLE_GRAFANA_CONNECTOR