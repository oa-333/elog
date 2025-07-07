#ifndef __ELOG_GRAFANA_JSON_TARGET_H__
#define __ELOG_GRAFANA_JSON_TARGET_H__

#include "elog_def.h"

#ifdef ELOG_ENABLE_GRAFANA_CONNECTOR

#ifndef ELOG_ENABLE_HTTP
#ifdef ELOG_MSVC
#error "Invalid configuration, Grafana connector requires HTTP client"
#else
#pragma GCC diagnostic error "Invalid configuration, Grafana connector requires HTTP client"
#endif
#endif

#ifndef ELOG_ENABLE_JSON
#ifdef ELOG_MSVC
#error "Invalid configuration, Grafana connector requires JSON"
#else
#pragma GCC diagnostic error "Invalid configuration, Grafana connector requires JSON"
#endif
#endif

#include <nlohmann/json.hpp>

#include "elog_grafana_target.h"
#include "elog_props_formatter.h"

namespace elog {

class ELOG_API ELogGrafanaJsonTarget : public ELogGrafanaTarget {
public:
    ELogGrafanaJsonTarget(const char* lokiEndpoint, uint32_t connectTimeoutMillis,
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
    ELogPropsFormatter m_labelFormatter;
    ELogPropsFormatter m_metadataFormatter;

    inline bool parseLabels(const std::string& labels) {
        return m_labelFormatter.parseProps(labels);
    }

    inline const std::vector<std::string>& getLabelNames() const {
        return m_labelFormatter.getPropNames();
    }

    inline void fillInLabels(const elog::ELogRecord& logRecord, elog::ELogFieldReceptor* receptor) {
        m_labelFormatter.fillInProps(logRecord, receptor);
    }

    inline bool parseMetadata(const std::string& labels) {
        return m_metadataFormatter.parseProps(labels);
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