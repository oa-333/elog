#ifndef __ELOG_OTEL_TARGET_H__
#define __ELOG_OTEL_TARGET_H__

#include "elog_def.h"

#ifdef ELOG_ENABLE_OTEL_CONNECTOR

#include <opentelemetry/exporters/otlp/otlp_http.h>
#include <opentelemetry/logs/logger.h>

#include "elog_mon_target.h"
#include "elog_props_formatter.h"

namespace logs_api = opentelemetry::logs;
namespace otlp_exporter = opentelemetry::exporter::otlp;
namespace nostd = opentelemetry::nostd;

#define ELOG_OTEL_DEFAULT_BATCH_EXPORT_QUEUE_SIZE 2048
#define ELOG_OTEL_DEFAULT_BATCH_EXPORT_TIMEOUT_MILLIS 5000
#define ELOG_OTEL_DEFAULT_BATCH_EXPORT_SIZE 512
#define ELOG_OTEL_DEFAULT_FLUSH_TIMEOUT_MILLIS 2000
#define ELOG_OTEL_DEFAULT_SHUTDOWN_TIMEOUT_MILLIS 5000

namespace elog {

/** @brief Open Telemetry export method to collector. */
enum class ELogOtelExportMethod { EM_HTTP, EM_GRPC };

class ELOG_API ELogOtelTarget : public ELogMonTarget {
public:
    ELogOtelTarget(ELogOtelExportMethod exportMethod = ELogOtelExportMethod::EM_HTTP,
                   const char* endpoint = "localhost:4318", const char* headers = "",
                   const char* compression = "", bool batching = false,
                   uint32_t batchQueueSize = ELOG_OTEL_DEFAULT_BATCH_EXPORT_QUEUE_SIZE,
                   uint32_t batchExportSize = ELOG_OTEL_DEFAULT_BATCH_EXPORT_SIZE,
                   uint64_t batchExportTimeMillis = ELOG_OTEL_DEFAULT_BATCH_EXPORT_TIMEOUT_MILLIS,
                   uint64_t flushTimeoutMillis = ELOG_OTEL_DEFAULT_FLUSH_TIMEOUT_MILLIS,
                   uint64_t shutdownTimeoutMillis = ELOG_OTEL_DEFAULT_SHUTDOWN_TIMEOUT_MILLIS,
                   opentelemetry::exporter::otlp::HttpRequestContentType contentType =
                       opentelemetry::exporter::otlp::HttpRequestContentType::kJson,
                   opentelemetry::exporter::otlp::JsonBytesMappingKind binaryEncoding =
                       opentelemetry::exporter::otlp::JsonBytesMappingKind::kBase64,
                   bool debug = false)
        : m_exportMethod(exportMethod),
          m_endpoint(endpoint),
          m_headers(headers),
          m_compression(compression),
          m_batching(batching),
          m_batchQueueSize(batchQueueSize),
          m_batchExportSize(batchExportSize),
          m_batchExportTimeMillis(batchExportTimeMillis),
          m_flushTimeoutMillis(flushTimeoutMillis),
          m_shutdownTimeoutMillis(shutdownTimeoutMillis),
          m_contentType(contentType),
          m_binaryEncoding(binaryEncoding),
          m_debug(debug),
          m_headersFormatter(nullptr) {}
    ELogOtelTarget(const ELogOtelTarget&) = delete;
    ELogOtelTarget(ELogOtelTarget&&) = delete;
    ELogOtelTarget& operator=(const ELogOtelTarget&) = delete;
    ~ELogOtelTarget() override {}

protected:
    /** @brief Order the log target to start (required for threaded targets). */
    bool startLogTarget() override;

    /** @brief Order the log target to stop (required for threaded targets). */
    bool stopLogTarget() override;

    /**
     * @brief Order the log target to write a log record (thread-safe).
     * @return The number of bytes written to log.
     */
    uint32_t writeLogRecord(const ELogRecord& logRecord) override;

    /** @brief Order the log target to flush. */
    bool flushLogTarget() override;

private:
    ELogOtelExportMethod m_exportMethod;
    std::string m_endpoint;
    std::string m_headers;
    std::string m_compression;
    bool m_batching;
    uint32_t m_batchQueueSize;
    uint32_t m_batchExportSize;
    uint64_t m_batchExportTimeMillis;
    uint64_t m_flushTimeoutMillis;
    uint64_t m_shutdownTimeoutMillis;
    opentelemetry::exporter::otlp::HttpRequestContentType m_contentType;
    opentelemetry::exporter::otlp::JsonBytesMappingKind m_binaryEncoding;
    bool m_debug;
    nostd::shared_ptr<logs_api::Logger> m_logger;
    ELogPropsFormatter* m_headersFormatter;
};

}  // namespace elog

#endif  // ELOG_ENABLE_OTEL_CONNECTOR

#endif  // __ELOG_OTEL_TARGET_H__