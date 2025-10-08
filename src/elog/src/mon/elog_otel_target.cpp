#include "mon/elog_otel_target.h"

#ifdef ELOG_ENABLE_OTEL_CONNECTOR

#include <opentelemetry/exporters/ostream/span_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_grpc_log_record_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_grpc_log_record_exporter_options.h>
#include <opentelemetry/exporters/otlp/otlp_http_log_record_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_http_log_record_exporter_options.h>
#include <opentelemetry/logs/provider.h>
#include <opentelemetry/sdk/logs/batch_log_record_processor_factory.h>
#include <opentelemetry/sdk/logs/batch_log_record_processor_options.h>
#include <opentelemetry/sdk/logs/logger_provider_factory.h>
#include <opentelemetry/sdk/logs/processor.h>
#include <opentelemetry/sdk/logs/simple_log_record_processor_factory.h>
#include <opentelemetry/sdk/trace/exporter.h>
#include <opentelemetry/sdk/trace/processor.h>
#include <opentelemetry/sdk/trace/simple_processor_factory.h>
#include <opentelemetry/sdk/trace/tracer_provider_factory.h>
#include <opentelemetry/trace/provider.h>

#include "elog.h"
#include "elog_common.h"
#include "elog_config_loader.h"
#include "elog_field_selector_internal.h"
#include "elog_internal.h"
#include "elog_logger.h"
#include "elog_report.h"

namespace trace_api = opentelemetry::trace;
namespace trace_sdk = opentelemetry::sdk::trace;
namespace trace_exporter = opentelemetry::exporter::trace;
namespace logs_sdk = opentelemetry::sdk::logs;

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogOtelTarget)

inline logs_api::Severity elogLevelToOtelLevel(ELogLevel logLevel) {
    switch (logLevel) {
        case ELEVEL_FATAL:
            return logs_api::Severity::kFatal;
        case ELEVEL_ERROR:
            return logs_api::Severity::kError;
        case ELEVEL_WARN:
            return logs_api::Severity::kWarn;
        case ELEVEL_NOTICE:
            return logs_api::Severity::kWarn2;
        case ELEVEL_INFO:
            return logs_api::Severity::kInfo;

        case ELEVEL_TRACE:
            return logs_api::Severity::kTrace;
        case ELEVEL_DEBUG:
            return logs_api::Severity::kDebug;
        case ELEVEL_DIAG:
        default:
            return logs_api::Severity::kDebug2;
    }
}

// field receptor for setting log records fields
class ELogOtelReceptor : public ELogFieldReceptor {
public:
    ELogOtelReceptor(nostd::unique_ptr<logs_api::LogRecord>& otelLogRecord)
        : ELogFieldReceptor(ELogFieldReceptor::ReceiveStyle::RS_BY_NAME),
          m_otelLogRecord(otelLogRecord) {}
    ELogOtelReceptor(const ELogOtelReceptor&) = delete;
    ELogOtelReceptor(ELogOtelReceptor&&) = delete;
    ELogOtelReceptor& operator=(const ELogOtelReceptor&) = delete;
    ~ELogOtelReceptor() final {}

    /** @brief Receives any static text found outside of log record field references. */
    void receiveStaticText(uint32_t typeId, const std::string& text,
                           const ELogFieldSpec& fieldSpec) {
        // should not arrive here, but in case the default log formatter was used, we just ignore it
    }

    /** @brief Receives the log time as unix epoch time in microseconds. */
    void receiveTimeEpoch(uint32_t typeId, uint64_t timeEpochMicros,
                          const ELogFieldSpec& fieldSpec) {
        m_otelLogRecord->SetTimestamp(
            opentelemetry::common::SystemTimestamp(std::chrono::microseconds(timeEpochMicros)));
        m_otelLogRecord->SetObservedTimestamp(
            opentelemetry::common::SystemTimestamp(std::chrono::microseconds(timeEpochMicros)));
    }

    /** @brief Receives the log record id. */
    void receiveRecordId(uint32_t typeId, uint64_t recordId, const ELogFieldSpec& fieldSpec) {
        m_otelLogRecord->SetAttribute("log.record.uid", recordId);
    }

    /** @brief Receives the host name. */
    void receiveHostName(uint32_t typeId, const char* hostName, const ELogFieldSpec& fieldSpec) {
        m_otelLogRecord->SetAttribute("client.address", hostName);
    }

    /** @brief Receives the user name. */
    void receiveUserName(uint32_t typeId, const char* userName, const ELogFieldSpec& fieldSpec) {
        m_otelLogRecord->SetAttribute("user.name", userName);
    }

    /** @brief Receives the OS name. */
    void receiveOsName(uint32_t typeId, const char* osName, const ELogFieldSpec& fieldSpec) {
        m_otelLogRecord->SetAttribute("os.name", osName);
    }

    /** @brief Receives the OS version. */
    void receiveOsVersion(uint32_t typeId, const char* osVersion, const ELogFieldSpec& fieldSpec) {
        m_otelLogRecord->SetAttribute("os.version", osVersion);
    }

    /** @brief Receives the application name. */
    void receiveAppName(uint32_t typeId, const char* appName, const ELogFieldSpec& fieldSpec) {
        m_otelLogRecord->SetAttribute("app.name", appName);
    }

    /** @brief Receives the program name. */
    void receiveProgramName(uint32_t typeId, const char* programName,
                            const ELogFieldSpec& fieldSpec) {
        m_otelLogRecord->SetAttribute("prog.name", programName);
    }

    /** @brief Receives the process id. */
    void receiveProcessId(uint32_t typeId, uint64_t processId, const ELogFieldSpec& fieldSpec) {
        m_otelLogRecord->SetAttribute("process.id", processId);
    }

    /** @brief Receives the thread id. */
    void receiveThreadId(uint32_t typeId, uint64_t threadId, const ELogFieldSpec& fieldSpec) {
        m_otelLogRecord->SetAttribute("thread.id", threadId);
    }

    /** @brief Receives the thread name. */
    void receiveThreadName(uint32_t typeId, const char* threadName,
                           const ELogFieldSpec& fieldSpec) {
        m_otelLogRecord->SetAttribute("thread.name", threadName);
    }

    /** @brief Receives the log source name. */
    void receiveLogSourceName(uint32_t typeId, const char* logSourceName,
                              const ELogFieldSpec& fieldSpec) {
        m_otelLogRecord->SetAttribute("log.source", logSourceName);
    }

    /** @brief Receives the module name. */
    void receiveModuleName(uint32_t typeId, const char* moduleName,
                           const ELogFieldSpec& fieldSpec) {
        m_otelLogRecord->SetAttribute("log.module", moduleName);
    }

    /** @brief Receives the file name. */
    void receiveFileName(uint32_t typeId, const char* fileName, const ELogFieldSpec& fieldSpec) {
        m_otelLogRecord->SetAttribute("log.file.path", fileName);
    }

    /** @brief Receives the logging line. */
    void receiveLineNumber(uint32_t typeId, uint64_t lineNumber, const ELogFieldSpec& fieldSpec) {
        m_otelLogRecord->SetAttribute("log.line", lineNumber);
    }

    /** @brief Receives the function name. */
    void receiveFunctionName(uint32_t typeId, const char* functionName,
                             const ELogFieldSpec& fieldSpec) {
        m_otelLogRecord->SetAttribute("log.function", functionName);
    }

    /** @brief Receives the log msg. */
    void receiveLogMsg(uint32_t typeId, const char* logMsg, const ELogFieldSpec& fieldSpec) {
        m_otelLogRecord->SetAttribute("log.record.original", logMsg);
    }

    /** @brief Receives a string log record field. */
    void receiveStringField(uint32_t typeId, const char* field, const ELogFieldSpec& fieldSpec,
                            size_t length) final {
        // we should not arrive here
    }

    /** @brief Receives an integer log record field. */
    void receiveIntField(uint32_t typeId, uint64_t field, const ELogFieldSpec& fieldSpec) final {
        // we should not arrive here
    }

    /** @brief Receives a time log record field. */
    void receiveTimeField(uint32_t typeId, const ELogTime& logTime, const char* timeStr,
                          const ELogFieldSpec& fieldSpec, size_t length) final {
        // time cannot be part of context
    }

    /** @brief Receives a log level log record field. */
    void receiveLogLevelField(uint32_t typeId, ELogLevel logLevel,
                              const ELogFieldSpec& fieldSpec) final {
        // log level cannot be part of context
    }

private:
    nostd::unique_ptr<logs_api::LogRecord>& m_otelLogRecord;
};

// field receptor for setting exporter headers
class ELogOtelHeaderReceptor : public ELogFieldReceptor {
public:
    ELogOtelHeaderReceptor() {}
    ELogOtelHeaderReceptor(const ELogOtelHeaderReceptor&) = delete;
    ELogOtelHeaderReceptor(ELogOtelHeaderReceptor&&) = delete;
    ELogOtelHeaderReceptor& operator=(const ELogOtelHeaderReceptor&) = delete;
    ~ELogOtelHeaderReceptor() final {}

    /** @brief Receives a string log record field. */
    void receiveStringField(uint32_t typeId, const char* field, const ELogFieldSpec& fieldSpec,
                            size_t length) final {
        m_propValues.push_back(field);
    }

    /** @brief Receives an integer log record field. */
    void receiveIntField(uint32_t typeId, uint64_t field, const ELogFieldSpec& fieldSpec) final {
        m_propValues.push_back(std::to_string(field));
    }

    /** @brief Receives a time log record field. */
    void receiveTimeField(uint32_t typeId, const ELogTime& logTime, const char* timeStr,
                          const ELogFieldSpec& fieldSpec, size_t length) final {
        m_propValues.push_back(timeStr);
    }

    /** @brief Receives a log level log record field. */
    void receiveLogLevelField(uint32_t typeId, ELogLevel logLevel,
                              const ELogFieldSpec& fieldSpec) final {
        m_propValues.push_back(elogLevelToStr(logLevel));
    }

    void applyHeaders(ELogPropsFormatter* formatter, otlp_exporter::OtlpHeaders& headers) {
        for (uint32_t i = 0; i < m_propValues.size(); ++i) {
            headers.insert(otlp_exporter::OtlpHeaders::value_type(formatter->getPropNameAt(i),
                                                                  m_propValues[i]));
        }
    }

private:
    std::vector<std::string> m_propValues;
};

// TODO: consider also supporting more exporters: Jaeger, Prometheus, Zipkin

static void applyHeaders(ELogPropsFormatter* headersFormatter,
                         otlp_exporter::OtlpHeaders& headers) {
    // use a dummy record, the headers can only relate to global fields anyway
    ELogRecord logRecord;
    ELogOtelHeaderReceptor receptor;
    headersFormatter->applyFieldSelectors(logRecord, &receptor);
    receptor.applyHeaders(headersFormatter, headers);
}

bool ELogOtelTarget::startLogTarget() {
    // create headers formatter if required
    ELogFormatter* headersFormatter = nullptr;
    if (!m_headers.empty()) {
        headersFormatter = ELogConfigLoader::loadLogFormatter(m_headers.c_str());
        if (headersFormatter == nullptr) {
            ELOG_REPORT_ERROR(
                "Failed to start Open Telemetry log target, invalid headers specification: %s",
                m_headers.c_str());
            return false;
        }
        if (strcmp(headersFormatter->getTypeName(), ELogPropsFormatter::TYPE_NAME) != 0) {
            ELOG_REPORT_ERROR("Invalid headers formatter, expecting '%s', seeing instead '%s': %s",
                              ELogPropsFormatter::TYPE_NAME, headersFormatter->getTypeName(),
                              m_headers.c_str());
            delete headersFormatter;
            return false;
        }
        m_headersFormatter = (ELogPropsFormatter*)headersFormatter;
    }

    // create OTEL log exporter via http or gRPC
    std::unique_ptr<opentelemetry::sdk::logs::LogRecordExporter> exporter;
    if (m_exportMethod == ELogOtelExportMethod::EM_HTTP) {
        otlp_exporter::OtlpHttpLogRecordExporterOptions opts;
        opts.url = m_endpoint + "/v1/logs";
        opts.content_type = m_contentType;
        opts.json_bytes_mapping = m_binaryEncoding;
        if (!m_headers.empty()) {
            applyHeaders(m_headersFormatter, opts.http_headers);
        }
        opts.timeout = std::chrono::duration_cast<std::chrono::system_clock::duration>(
            std::chrono::milliseconds(m_batchExportTimeMillis));
        opts.compression = m_compression;
        opts.console_debug = m_debug;
        exporter = otlp_exporter::OtlpHttpLogRecordExporterFactory::Create(opts);
    } else {
        otlp_exporter::OtlpGrpcLogRecordExporterOptions opts;
        opts.endpoint = m_endpoint;
        opts.timeout = std::chrono::duration_cast<std::chrono::system_clock::duration>(
            std::chrono::milliseconds(m_batchExportTimeMillis));
        if (!m_headers.empty()) {
            applyHeaders(m_headersFormatter, opts.metadata);
        }
        opts.compression = m_compression;
        exporter = otlp_exporter::OtlpGrpcLogRecordExporterFactory::Create(opts);
    }

    // configure batching
    logs_sdk::BatchLogRecordProcessorOptions batchOpts;
    if (m_batching) {
        batchOpts.max_queue_size = m_batchQueueSize;
        batchOpts.schedule_delay_millis = std::chrono::milliseconds(m_batchExportTimeMillis);
        batchOpts.max_export_batch_size = m_batchExportSize;
    }

    // create a logger on top of the exporter exports to OTEL via http or gRPC
    std::unique_ptr<logs_sdk::LogRecordProcessor> processor =
        m_batching
            ? logs_sdk::BatchLogRecordProcessorFactory::Create(std::move(exporter), batchOpts)
            : logs_sdk::SimpleLogRecordProcessorFactory::Create(std::move(exporter));
    std::shared_ptr<logs_api::LoggerProvider> provider =
        logs_sdk::LoggerProviderFactory::Create(std::move(processor));
    logs_api::Provider::SetLoggerProvider(provider);

    m_logger = provider->GetLogger("elog_otel", "elog", "0.1.6");
    if (m_logger.get() == nullptr) {
        ELOG_REPORT_ERROR("Failed to initialize Open Telemtry logger");
        if (m_headersFormatter != nullptr) {
            delete m_headersFormatter;
            m_headersFormatter = nullptr;
        }
        return false;
    }

    return true;
}

bool ELogOtelTarget::stopLogTarget() {
    // dig out the logger provider and call shutdown for full cleanup
    nostd::shared_ptr<logs_api::LoggerProvider> loggerProvider =
        logs_api::Provider::GetLoggerProvider();
    logs_sdk::LoggerProvider* sdkLoggerProvider =
        dynamic_cast<logs_sdk::LoggerProvider*>(loggerProvider.get());
    if (sdkLoggerProvider != nullptr) {
        if (!sdkLoggerProvider->Shutdown(std::chrono::milliseconds(m_shutdownTimeoutMillis))) {
            ELOG_REPORT_WARN(
                "Failed to fully shutdown Open Telemetry log target, operation timed out");
        }
    }

    m_logger = nullptr;
    std::shared_ptr<logs_api::LoggerProvider> none;
    logs_api::Provider::SetLoggerProvider(none);
    if (m_headersFormatter != nullptr) {
        delete m_headersFormatter;
        m_headersFormatter = nullptr;
    }
    return true;
}

uint32_t ELogOtelTarget::writeLogRecord(const ELogRecord& logRecord) {
    // create log record and fill in attributes
    nostd::unique_ptr<logs_api::LogRecord> otelLogRecord = m_logger->CreateLogRecord();
    ELogOtelReceptor receptor(otelLogRecord);
    ELogFormatter* formatter = getLogFormatter();
    if (formatter == nullptr) {
        formatter = getDefaultLogFormatter();
    }
    formatter->applyFieldSelectors(logRecord, &receptor);

    // emit log record, either to be saved in a batch or sent directly to the collector
    m_logger->EmitLogRecord(std::move(otelLogRecord));

    // no statistics yet
    return 0;
}

bool ELogOtelTarget::flushLogTarget() {
    // dig out the logger provider and call force flush
    nostd::shared_ptr<logs_api::LoggerProvider> loggerProvider =
        logs_api::Provider::GetLoggerProvider();
    logs_sdk::LoggerProvider* sdkLoggerProvider =
        dynamic_cast<logs_sdk::LoggerProvider*>(loggerProvider.get());
    if (sdkLoggerProvider != nullptr) {
        if (!sdkLoggerProvider->ForceFlush(std::chrono::milliseconds(m_flushTimeoutMillis))) {
            ELOG_REPORT_WARN("Failed to flush Open Telemetry log target, operation timed out");
        }
    }
    return true;
}

}  // namespace elog

#endif  // ELOG_ENABLE_GRAFANA_CONNECTOR