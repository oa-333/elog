#include "mon/elog_otel_target_provider.h"

#ifdef ELOG_ENABLE_OTEL_CONNECTOR

#include "elog_common.h"
#include "elog_config_loader.h"
#include "elog_report.h"
#include "mon/elog_otel_target.h"

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogOtelTargetProvider)

static bool loadExportMethod(const ELogConfigMapNode* logTargetCfg,
                             ELogOtelExportMethod& exportMethod) {
    std::string methodStr;
    if (!ELogConfigLoader::getLogTargetStringProperty(logTargetCfg, "Otel", "method", methodStr)) {
        return false;
    }

    // parse export method
    if (methodStr.compare("http") == 0) {
        exportMethod = ELogOtelExportMethod::EM_HTTP;
    } else if (methodStr.compare("grpc") == 0) {
        exportMethod = ELogOtelExportMethod::EM_GRPC;
    } else {
        ELOG_REPORT_ERROR(
            "Failed to load Open Telemtry log target, invalid OTLP export method: %s (context: %s)",
            methodStr.c_str(), logTargetCfg->getFullContext());
        return false;
    }

    return true;
}

static bool loadContentType(const ELogConfigMapNode* logTargetCfg,
                            opentelemetry::exporter::otlp::HttpRequestContentType& contentType) {
    std::string contentTypeStr;
    bool found = false;
    if (!ELogConfigLoader::getOptionalLogTargetStringProperty(logTargetCfg, "Otel", "content_type",
                                                              contentTypeStr, &found)) {
        return false;
    }
    if (found) {
        if (contentTypeStr.compare("json") == 0) {
            contentType = opentelemetry::exporter::otlp::HttpRequestContentType::kJson;
        } else if (contentTypeStr.compare("binary") == 0) {
            contentType = opentelemetry::exporter::otlp::HttpRequestContentType::kBinary;
        } else {
            ELOG_REPORT_ERROR(
                "Failed to load Open Telemtry log target, invalid HTTP content type: %s "
                "(context: %s)",
                contentTypeStr.c_str(), logTargetCfg->getFullContext());
            return false;
        }
    }

    return true;
}

static bool loadBinaryEncoding(
    const ELogConfigMapNode* logTargetCfg,
    opentelemetry::exporter::otlp::JsonBytesMappingKind& binaryEncoding) {
    std::string binaryEncodingStr;
    bool found = false;
    if (!ELogConfigLoader::getOptionalLogTargetStringProperty(
            logTargetCfg, "Otel", "binary_encoding", binaryEncodingStr, &found)) {
        return false;
    }
    if (found) {
        if (binaryEncodingStr.compare("hex") == 0) {
            binaryEncoding = opentelemetry::exporter::otlp::JsonBytesMappingKind::kHex;
        } else if (binaryEncodingStr.compare("hexid") == 0) {
            binaryEncoding = opentelemetry::exporter::otlp::JsonBytesMappingKind::kHexId;
        } else if (binaryEncodingStr.compare("base64") == 0) {
            binaryEncoding = opentelemetry::exporter::otlp::JsonBytesMappingKind::kBase64;
        } else {
            ELOG_REPORT_ERROR(
                "Failed to load Open Telemtry log target, invalid HTTP binary encoding: %s "
                "(context: %s)",
                binaryEncodingStr.c_str(), logTargetCfg->getFullContext());
            return false;
        }
    }

    return true;
}

ELogTarget* ELogOtelTargetProvider::loadTarget(const ELogConfigMapNode* logTargetCfg) {
    // expected url is as follows:
    // mon://otel?method=[http/grpc]
    //  endpoint=[http/https]://host:port&  // both http and grpc
    //  headers=[props log format]&         // both http and grpc
    //  compression=[value]                 // both http and grpc
    //  batching=yes/no&                    // both http and grpc
    //  batch_queue_size=[value]&           // both http and grpc
    //  batch_export_timeout=[value]&       // both http and grpc
    //  batch_export_size=[value]&          // both http and grpc
    //  flush_timeout=[value]&              // both http and grpc
    //  shutdown_timeout=[value]            // both http and grpc
    //  content_type=[json/binary]&         // http only
    //  binary_encoding=[hex/hexid/base64]& // http only
    //  debug=[yes/no]&                     // http only

    // load OTLP export method (http/gRPC)
    ELogOtelExportMethod exportMethod = ELogOtelExportMethod::EM_HTTP;
    if (!loadExportMethod(logTargetCfg, exportMethod)) {
        return nullptr;
    }

    // parse endpoint
    std::string endpoint;
    if (!ELogConfigLoader::getLogTargetStringProperty(logTargetCfg, "Otel", "endpoint", endpoint)) {
        return nullptr;
    }

    // parse optional headers (both http and grpc exporters)
    // this should be a log format string (should be qualified, preferably 'props' so the result is
    // a map that can be used for HTTP headers "key:value")
    std::string headers;
    if (!ELogConfigLoader::getOptionalLogTargetStringProperty(logTargetCfg, "Otel", "headers",
                                                              headers)) {
        return nullptr;
    }

    // parse optional compression type (both http and grpc exporters)
    // it is unclear yet what are valid values
    std::string compression;
    if (!ELogConfigLoader::getOptionalLogTargetStringProperty(logTargetCfg, "Otel", "compression",
                                                              compression)) {
        return nullptr;
    }

    bool batching = false;
    uint32_t queueSize = ELOG_OTEL_DEFAULT_BATCH_EXPORT_QUEUE_SIZE;
    uint64_t exportTimeoutMillis = ELOG_OTEL_DEFAULT_BATCH_EXPORT_TIMEOUT_MILLIS;
    uint32_t exportSize = ELOG_OTEL_DEFAULT_BATCH_EXPORT_SIZE;
    if (!ELogConfigLoader::getOptionalLogTargetBoolProperty(logTargetCfg, "Otel", "batching",
                                                            batching)) {
        return nullptr;
    }
    if (batching) {
        // parse optional batch queue size
        if (!ELogConfigLoader::getOptionalLogTargetUInt32Property(logTargetCfg, "Otel",
                                                                  "batch_queue_size", queueSize)) {
            return nullptr;
        }

        // parse optional export timeout
        if (!ELogConfigLoader::getOptionalLogTargetTimeoutProperty(
                logTargetCfg, "Otel", "batch_export_timeout", exportTimeoutMillis,
                ELogTimeUnits::TU_MILLI_SECONDS)) {
            return nullptr;
        }

        // parse optional batch queue size
        if (!ELogConfigLoader::getOptionalLogTargetUInt32Property(
                logTargetCfg, "Otel", "batch_export_size", exportSize)) {
            return nullptr;
        }
    }

    // flush timeout
    uint64_t flushTimeoutMillis = ELOG_OTEL_DEFAULT_FLUSH_TIMEOUT_MILLIS;
    if (!ELogConfigLoader::getOptionalLogTargetTimeoutProperty(logTargetCfg, "Otel",
                                                               "flush_timeout", flushTimeoutMillis,
                                                               ELogTimeUnits::TU_MILLI_SECONDS)) {
        return nullptr;
    }

    // shutdown timeout
    uint64_t shutdownTimeoutMillis = ELOG_OTEL_DEFAULT_SHUTDOWN_TIMEOUT_MILLIS;
    if (!ELogConfigLoader::getOptionalLogTargetTimeoutProperty(
            logTargetCfg, "Otel", "shutdown_timeout", shutdownTimeoutMillis,
            ELogTimeUnits::TU_MILLI_SECONDS)) {
        return nullptr;
    }

    // in case of http export method we have a few more options
    opentelemetry::exporter::otlp::HttpRequestContentType contentType =
        opentelemetry::exporter::otlp::HttpRequestContentType::kJson;
    opentelemetry::exporter::otlp::JsonBytesMappingKind binaryEncoding =
        opentelemetry::exporter::otlp::JsonBytesMappingKind::kBase64;
    bool debug = false;
    if (exportMethod == ELogOtelExportMethod::EM_HTTP) {
        if (!loadContentType(logTargetCfg, contentType)) {
            return nullptr;
        }
        if (contentType == opentelemetry::exporter::otlp::HttpRequestContentType::kBinary) {
            if (!loadBinaryEncoding(logTargetCfg, binaryEncoding)) {
                return nullptr;
            }
        }
        if (!ELogConfigLoader::getOptionalLogTargetBoolProperty(logTargetCfg, "Otel", "debug",
                                                                debug)) {
            return nullptr;
        }
    }

    // create log target
    ELogOtelTarget* target = new (std::nothrow)
        ELogOtelTarget(exportMethod, endpoint.c_str(), headers.c_str(), compression.c_str(),
                       batching, queueSize, exportSize, exportTimeoutMillis, flushTimeoutMillis,
                       shutdownTimeoutMillis, contentType, binaryEncoding, debug);
    if (target == nullptr) {
        ELOG_REPORT_ERROR("Failed to allocate Open Telemtry log target, out of memory");
    }
    return target;
}

}  // namespace elog

#endif  // ELOG_ENABLE_GRAFANA_CONNECTOR
