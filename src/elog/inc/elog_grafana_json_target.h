#ifndef __ELOG_GRAFANA_JSON_TARGET_H__
#define __ELOG_GRAFANA_JSON_TARGET_H__

#include "elog_def.h"

#define ELOG_ENABLE_GRAFANA_CONNECTOR
#ifdef ELOG_ENABLE_GRAFANA_CONNECTOR

#include <nlohmann/json.hpp>
#include <unordered_map>

#include "elog_grafana_target.h"
#include "elog_props_formatter.h"

namespace elog {

// TODO: consider adding dependency on json, and have this class defined in its own file
class ELOG_API ELogJsonFormatter : public ELogBaseFormatter {
public:
    ELogJsonFormatter() {}
    ELogJsonFormatter(const ELogJsonFormatter&) = delete;
    ELogJsonFormatter(ELogJsonFormatter&&) = delete;
    ~ELogJsonFormatter() override {}

    bool parseJson(const std::string& jsonStr);

    inline void fillInProps(const ELogRecord& logRecord, elog::ELogFieldReceptor* receptor) {
        applyFieldSelectors(logRecord, receptor);
    }

    inline const std::string& getPropNameAt(uint32_t index) const { return m_propNames[index]; }

    inline uint32_t getPropCount() const { return m_propNames.size(); }

    inline const std::vector<std::string>& getPropNames() const { return m_propNames; }

private:
    nlohmann::json m_jsonField;
    std::vector<std::string> m_propNames;
};

class ELOG_API ELogGrafanaJsonTarget : public ELogGrafanaTarget {
public:
    ELogGrafanaJsonTarget(const std::string& lokiEndpoint, uint32_t connectTimeoutMillis,
                          uint32_t writeTimeoutMillis, uint32_t readTimeoutMillis,
                          const char* labels, const char* logLineMetadata)
        : ELogGrafanaTarget(lokiEndpoint, connectTimeoutMillis, writeTimeoutMillis,
                            readTimeoutMillis),
          m_labels(labels),
          m_logLineMetadata(logLineMetadata) {}

    ELogGrafanaJsonTarget(const ELogGrafanaJsonTarget&) = delete;
    ELogGrafanaJsonTarget(ELogGrafanaJsonTarget&&) = delete;
    ~ELogGrafanaJsonTarget() final {}

protected:
    /** @brief Order the log target to start (required for threaded targets). */
    bool startLogTarget() final;

    /** @brief Sends a log record to a log target. */
    uint32_t writeLogRecord(const ELogRecord& logRecord) final;

    /** @brief Orders a buffered log target to flush it log messages. */
    void flushLogTarget() final;

private:
    std::string m_labels;
    std::string m_logLineMetadata;
    nlohmann::json m_logEntry;
    ELogJsonFormatter m_labelFormatter;
    ELogJsonFormatter m_metadataFormatter;

    inline bool parseLabels(const std::string& labels) {
        return m_labelFormatter.parseJson(labels);
    }

    inline const std::vector<std::string>& getLabelNames() const {
        return m_labelFormatter.getPropNames();
    }

    inline void fillInLabels(const elog::ELogRecord& logRecord, elog::ELogFieldReceptor* receptor) {
        m_labelFormatter.fillInProps(logRecord, receptor);
    }

    inline bool parseMetadata(const std::string& labels) {
        return m_metadataFormatter.parseJson(labels);
    }

    inline const std::vector<std::string>& getMetadataNames() const {
        return m_metadataFormatter.getPropNames();
    }

    inline void fillInMetadata(const elog::ELogRecord& logRecord,
                               elog::ELogFieldReceptor* receptor) {
        m_metadataFormatter.fillInProps(logRecord, receptor);
    }
};

}  // namespace elog

#endif  // ELOG_ENABLE_GRAFANA_CONNECTOR

#endif  // __ELOG_GRAFANA_JSON_TARGET_H__