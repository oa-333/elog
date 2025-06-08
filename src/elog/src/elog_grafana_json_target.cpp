#include "elog_grafana_json_target.h"

#ifdef ELOG_ENABLE_GRAFANA_CONNECTOR

#include "elog_common.h"
#include "elog_error.h"

namespace elog {

class ELogGrafanaJsonReceptor : public ELogFieldReceptor {
public:
    ELogGrafanaJsonReceptor() {}
    ~ELogGrafanaJsonReceptor() final {}

    /** @brief Receives a string log record field. */
    void receiveStringField(uint32_t typeId, const char* field, const ELogFieldSpec& fieldSpec,
                            size_t length) {
        m_propValues.push_back(field);
    }

    /** @brief Receives an integer log record field. */
    void receiveIntField(uint32_t typeId, uint64_t field, const ELogFieldSpec& fieldSpec) final {
        m_propValues.push_back(std::to_string(field));
    }

    /** @brief Receives a time log record field. */
    void receiveTimeField(uint32_t typeId, const ELogTime& logTime, const char* timeStr,
                          const ELogFieldSpec& fieldSpec) final {
        m_propValues.push_back(timeStr);
    }

    /** @brief Receives a log level log record field. */
    void receiveLogLevelField(uint32_t typeId, ELogLevel logLevel,
                              const ELogFieldSpec& fieldSpec) final {
        const char* logLevelStr = elogLevelToStr(logLevel);
        m_propValues.push_back(logLevelStr);
    }

    bool prepareJsonMap(nlohmann::json& logEntry, const std::vector<std::string>& propNames) {
        if (m_propValues.size() != propNames.size()) {
            ELOG_REPORT_ERROR(
                "Mismatching JSON property names and values (%u names, %u values) in Grafana JSON "
                "receptor",
                propNames.size(), m_propValues.size());
            return false;
        }
        for (uint32_t i = 0; i < m_propValues.size(); ++i) {
            logEntry[propNames[i]] = m_propValues[i];
        }
        return true;
    }

private:
    std::vector<std::string> m_propValues;
};

bool ELogJsonFormatter::parseJson(const std::string& jsonStr) {
    try {
        // parse json and iterate over all items
        ELOG_REPORT_TRACE("Parsing json string: %s", jsonStr.c_str());
        m_jsonField = nlohmann::json::parse(jsonStr);
        for (const auto& item : m_jsonField.items()) {
            // take property name
            ELOG_REPORT_TRACE("Iterating property name %s", item.key().c_str());
            m_propNames.push_back(item.key());
            std::string value = trim(((std::string)item.value().get<std::string>()));

            // check if this is a field reference
            if (value.find("${") == 0) {
                // verify field reference syntax
                if (value.back() != '}') {
                    ELOG_REPORT_ERROR(
                        "Invalid field specification, missing closing curly brace, while parsing "
                        "JSON string '%s' at Grafana log target");
                    return false;
                }

                // extract field spec string and parse
                std::string valueStr = value.substr(2, value.size() - 2);
                ELogFieldSpec fieldSpec;
                if (!parseFieldSpec(valueStr, fieldSpec)) {
                    ELOG_REPORT_ERROR(
                        "Failed to parse json value '%s' for key '%s' at Grafana log target",
                        item.value().dump().c_str(), item.key().c_str());
                    return false;
                }

                // now collect the field
                ELOG_REPORT_TRACE("Extracted field spec: %s", fieldSpec.m_name.c_str());
                if (!handleField(fieldSpec)) {
                    return false;
                }
            } else {
                // otherwise, this is plain static text
                ELOG_REPORT_TRACE("Extracted static text value: %s", value.c_str());
                if (!handleText(value)) {
                    return false;
                }
            }
            ELOG_REPORT_TRACE("Parsed json property: %s=%s", item.key().c_str(), value.c_str());
        }
    } catch (nlohmann::json::parse_error& pe) {
        ELOG_REPORT_ERROR("Failed to parse json string '%s' at Grafana log target: %s",
                          jsonStr.c_str(), pe.what());
        return false;
    }
    return true;
}

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
        ELogGrafanaJsonReceptor receptor;
        fillInLabels(logRecord, &receptor);
        if (!receptor.prepareJsonMap(m_logEntry["streams"][0]["stream"], getLabelNames())) {
            return 0;
        }
    }

    // log line time, common to all log lines until flush
    auto& values = m_logEntry["streams"][0]["values"];
    uint32_t logLineCount = values.size();
    auto& logLine = values[logLineCount];
    logLine[0] =
        std::to_string(std::chrono::nanoseconds(elogTimeToUTCNanos(logRecord.m_logTime)).count());

    // log line
    std::string logMsg;
    formatLogMsg(logRecord, logMsg);
    logLine[1] = logMsg;

    // fill int log line attributes
    if (m_metadataFormatter.getPropCount() > 0) {
        ELogGrafanaJsonReceptor receptor;
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