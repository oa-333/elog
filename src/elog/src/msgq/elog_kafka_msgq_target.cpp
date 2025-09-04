#include "msgq/elog_kafka_msgq_target.h"

#ifdef ELOG_ENABLE_KAFKA_MSGQ_CONNECTOR

#include <cstring>
#include <sstream>

#include "elog_field_selector_internal.h"
#include "elog_report.h"

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogKafkaMsgQTarget)

static const uint32_t ELOG_DEFAULT_KAFKA_SHUTDOWN_FLUSH_TIMEOUT_MILLIS = 5000;
static const uint32_t ELOG_DEFAULT_KAFKA_FLUSH_TIMEOUT_MILLIS = 100;

class ELogKafkaMsgQFieldReceptor : public ELogFieldReceptor {
public:
    ELogKafkaMsgQFieldReceptor() {}
    ELogKafkaMsgQFieldReceptor(const ELogKafkaMsgQFieldReceptor&) = delete;
    ELogKafkaMsgQFieldReceptor(ELogKafkaMsgQFieldReceptor&&) = delete;
    ELogKafkaMsgQFieldReceptor& operator=(const ELogKafkaMsgQFieldReceptor&) = delete;
    ~ELogKafkaMsgQFieldReceptor() final {}

    /** @brief Receives a string log record field. */
    void receiveStringField(uint32_t typeId, const char* field, const ELogFieldSpec& fieldSpec,
                            size_t length) final {
        m_headerValues.push_back(field);
    }

    /** @brief Receives an integer log record field. */
    void receiveIntField(uint32_t typeId, uint64_t field, const ELogFieldSpec& fieldSpec) final {
        m_headerValues.push_back(std::to_string(field));
    }

    /** @brief Receives a time log record field. */
    void receiveTimeField(uint32_t typeId, const ELogTime& logTime, const char* timeStr,
                          const ELogFieldSpec& fieldSpec, size_t length) final {
        m_headerValues.push_back(timeStr);
    }

    /** @brief Receives a log level log record field. */
    void receiveLogLevelField(uint32_t typeId, ELogLevel logLevel,
                              const ELogFieldSpec& fieldSpec) final {
        const char* logLevelStr = elogLevelToStr(logLevel);
        m_headerValues.push_back(logLevelStr);
    }

    bool prepareHeaders(rd_kafka_headers_t* headers, const std::vector<std::string>& headerNames,
                        uint32_t& bytesWritten) {
        if (m_headerValues.size() != headerNames.size()) {
            ELOG_REPORT_ERROR("Mismatching header names a values (%u names, %u values)",
                              headerNames.size(), m_headerValues.size());
            return false;
        }
        for (uint32_t i = 0; i < m_headerValues.size(); ++i) {
            rd_kafka_resp_err_t res = rd_kafka_header_add(
                headers, headerNames[i].c_str(), (ssize_t)headerNames[i].length(),
                (void*)m_headerValues[i].c_str(), (ssize_t)m_headerValues[i].length());
            if (res != RD_KAFKA_RESP_ERR_NO_ERROR) {
                ELOG_REPORT_ERROR("Failed to add kafka message header %s=%s: %s",
                                  headerNames[i].c_str(), m_headerValues[i].c_str(),
                                  rd_kafka_err2name(res));
                return false;
            } else {
                // NOTE: no overflow is expected here
                bytesWritten += (uint32_t)m_headerValues[i].length();
            }
        }
        return true;
    }

private:
    std::vector<std::string> m_headerValues;
};

bool ELogKafkaMsgQTarget::startLogTarget() {
    // parse the headers with log record field selector tokens
    // this builds a processed statement text with questions marks instead of log record field
    // references, and also prepares the field selector array
    if (!parseHeaders(m_headers)) {
        return false;
    }
    char errstr[512];

    m_conf = rd_kafka_conf_new();
    if (m_clientId.empty()) {
        formatClientId();
    }

    if (rd_kafka_conf_set(m_conf, "client.id", m_clientId.c_str(), errstr, sizeof(errstr)) !=
        RD_KAFKA_CONF_OK) {
        ELOG_REPORT_ERROR("Failed to create kafka configuration object: %s", errstr);
        cleanup();
        return false;
    }

    if (rd_kafka_conf_set(m_conf, "bootstrap.servers", m_bootstrapServers.c_str(), errstr,
                          sizeof(errstr)) != RD_KAFKA_CONF_OK) {
        ELOG_REPORT_ERROR("Failed to configure kafka bootstrap servers '%s': %s",
                          m_bootstrapServers.c_str(), errstr);
        cleanup();
        return false;
    }

    // TODO: this should be configurable
    m_topicConf = rd_kafka_topic_conf_new();
    if (rd_kafka_topic_conf_set(m_topicConf, "acks", "all", errstr, sizeof(errstr)) !=
        RD_KAFKA_CONF_OK) {
        ELOG_REPORT_ERROR("Failed to configure kafka topic acks=all: %s", errstr);
        cleanup();
        return false;
    }

    // create producer
    m_producer = rd_kafka_new(RD_KAFKA_PRODUCER, m_conf, errstr, sizeof(errstr));
    if (m_producer == nullptr) {
        ELOG_REPORT_ERROR("Failed to create kafka producer object: %s", errstr);
        cleanup();
        return false;
    }
    // NOTE: after successful call to rd_kafka_new(), the configuration object is no longer usable
    // and must not be destroyed, according to API documentation
    m_conf = nullptr;

    // create topic
    m_topic = rd_kafka_topic_new(m_producer, m_topicName.c_str(), m_topicConf);
    if (m_topic == nullptr) {
        ELOG_REPORT_ERROR("Failed to create kafka topic %s: %s", m_topicName.c_str(),
                          rd_kafka_err2name(rd_kafka_last_error()));
        cleanup();
        return false;
    }
    // NOTE: after successful call to rd_kafka_topic_new(), the topic configuration object is no
    // longer usable and must not be destroyed, according to API documentation
    m_topicConf = nullptr;

    return true;
}

bool ELogKafkaMsgQTarget::stopLogTarget() {
    // wait for 5 seconds for all produces messages to be flushed
    uint64_t flushTimeoutMillis = m_shutdownFlushTimeoutMillis;
    if (flushTimeoutMillis == 0) {
        flushTimeoutMillis = ELOG_DEFAULT_KAFKA_SHUTDOWN_FLUSH_TIMEOUT_MILLIS;
    }
    rd_kafka_resp_err_t res = rd_kafka_flush(m_producer, (int)flushTimeoutMillis);
    if (res != RD_KAFKA_RESP_ERR_NO_ERROR) {
        ELOG_REPORT_ERROR("Failed to flush kafka topic producer: %s", rd_kafka_err2name(res));
        return false;
    }

    // now just cleanup
    cleanup();
    return true;
}

uint32_t ELogKafkaMsgQTarget::writeLogRecord(const ELogRecord& logRecord) {
    // prepare headers if any
    // NOTE: receptor must live until message sending, because it holds value strings
    uint32_t bytesWritten = 0;
    ELogKafkaMsgQFieldReceptor receptor;
    rd_kafka_headers_t* headers = nullptr;
    if (!m_headers.empty()) {
        headers = rd_kafka_headers_new(getHeaderCount());
        if (headers == nullptr) {
            ELOG_REPORT_ERROR("Failed to allocate kafka headers, out of memory");
            return 0;
        }
        fillInHeaders(logRecord, &receptor);
        if (!receptor.prepareHeaders(headers, getHeaderNames(), bytesWritten)) {
            ELOG_REPORT_ERROR("Failed to prepare kafka message headers");
            return 0;
        }
    }

    // prepare formatted log message
    std::string logMsg;
    formatLogMsg(logRecord, logMsg);
    bytesWritten += (uint32_t)logMsg.length();

    // unassigned partition, copy payload, no key specification, payload is formatted string
    // headers include specific log record fields
    int32_t partition = RD_KAFKA_PARTITION_UA;
    if (m_partition != -1) {
        partition = m_partition;
    }
    if (headers != nullptr) {
        const uint32_t VU_COUNT = 6;
        rd_kafka_vu_t vus[VU_COUNT];
        vus[0].vtype = RD_KAFKA_VTYPE_PARTITION;
        vus[0].u.i32 = partition;

        vus[1].vtype = RD_KAFKA_VTYPE_MSGFLAGS;
        vus[1].u.i = RD_KAFKA_MSG_F_COPY;

        vus[2].vtype = RD_KAFKA_VTYPE_VALUE;
        vus[2].u.mem.ptr = (void*)logMsg.c_str();
        vus[2].u.mem.size = logMsg.length();

        vus[3].vtype = RD_KAFKA_VTYPE_KEY;
        vus[3].u.mem.ptr = nullptr;
        vus[3].u.mem.size = 0;

        vus[4].vtype = RD_KAFKA_VTYPE_HEADERS;
        vus[4].u.headers = headers;

        vus[5].vtype = RD_KAFKA_VTYPE_TOPIC;
        vus[5].u.cstr = m_topicName.c_str();
        rd_kafka_error_t* res = rd_kafka_produceva(m_producer, vus, VU_COUNT);
        if (res != nullptr) {
            const char* errMsg = rd_kafka_err2name(rd_kafka_error_code(res));
            ELOG_REPORT_ERROR("Failed to produce message on kafka topic %s: %s",
                              m_topicName.c_str(), errMsg);
            rd_kafka_error_destroy(res);
        }
    } else {
        if (rd_kafka_produce(m_topic, partition, RD_KAFKA_MSG_F_COPY, (void*)logMsg.c_str(),
                             logMsg.length(), nullptr, 0, nullptr) == -1) {
            const char* errMsg = rd_kafka_err2name(rd_kafka_last_error());
            ELOG_REPORT_ERROR("Failed to produce message on kafka topic %s: %s",
                              m_topicName.c_str(), errMsg);
        }
    }

    return bytesWritten;
}

bool ELogKafkaMsgQTarget::flushLogTarget() {
    uint64_t flushTimeoutMillis = m_flushTimeoutMillis;
    if (flushTimeoutMillis == 0) {
        flushTimeoutMillis = ELOG_DEFAULT_KAFKA_FLUSH_TIMEOUT_MILLIS;
    }
    rd_kafka_resp_err_t res = rd_kafka_flush(m_producer, (int)flushTimeoutMillis);
    if (res != RD_KAFKA_RESP_ERR_NO_ERROR) {
        ELOG_REPORT_ERROR("Failed to flush kafka topic producer: %s", rd_kafka_err2name(res));
        return false;
    }
    return true;
}

void ELogKafkaMsgQTarget::formatClientId() {
    std::stringstream s;
    s << getHostName() << "." << getUserName() << "." << getProgramName() << ".";
#ifdef ELOG_WINDOWS
    s << GetCurrentProcessId();
#else
    s << getpid();
#endif  // ELOG_WINDOWS
    m_clientId = s.str();
}

void ELogKafkaMsgQTarget::cleanup() {
    if (m_topic != nullptr) {
        rd_kafka_topic_destroy(m_topic);
        m_topic = nullptr;
    }
    if (m_producer != nullptr) {
        rd_kafka_destroy(m_producer);
        m_producer = nullptr;
    }
    if (m_topicConf != nullptr) {
        rd_kafka_topic_conf_destroy(m_topicConf);
        m_topicConf = nullptr;
    }
    if (m_conf != nullptr) {
        rd_kafka_conf_destroy(m_conf);
        m_conf = nullptr;
    }
}

}  // namespace elog

#endif  // ELOG_ENABLE_MYSQL_DB_CONNECTOR