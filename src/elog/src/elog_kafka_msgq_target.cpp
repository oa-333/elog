#include "elog_kafka_msgq_target.h"

#ifdef ELOG_ENABLE_KAFKA_MSGQ_CONNECTOR

#include <cstring>

#include "elog_system.h"

namespace elog {

static const uint32_t ELOG_DEFAULT_KAFKA_SHUTDOWN_FLUSH_TIMEOUT_MILLIS = 5000;
static const uint32_t ELOG_DEFAULT_KAFKA_FLUSH_TIMEOUT_MILLIS = 100;

class ELogKafkaMsgQFieldReceptor : public ELogFieldReceptor {
public:
    ELogKafkaMsgQFieldReceptor() {}
    ~ELogKafkaMsgQFieldReceptor() final {}

    /** @brief Receives a string log record field. */
    void receiveStringField(const std::string& field, int justify) {
        m_stringCache.push_back(field);
    }

    /** @brief Receives an integer log record field. */
    void receiveIntField(uint64_t field, int justify) final {
        // it really depends
        m_stringCache.push_back(std::to_string(field));
    }

#ifdef ELOG_MSVC
    /** @brief Receives a time log record field. */
    void receiveTimeField(const SYSTEMTIME& sysTime, const char* timeStr, int justify) final {
        int res = sqlite3_bind_text(m_stmt, m_fieldNum++, timeStr, -1, SQLITE_TRANSIENT);
        if (m_res == 0) {
            m_res = res;
        }
    }
#else
    /** @brief Receives a time log record field. */
    void receiveTimeField(const timeval& sysTime, const char* timeStr, int justify) final {
        m_stringCache.push_back(timeStr);
    }
#endif

    /** @brief Receives a log level log record field. */
    void receiveLogLevelField(ELogLevel logLevel, int justify) final {
        const char* logLevelStr = elogLevelToStr(logLevel);
        m_stringCache.push_back(logLevelStr);
    }

    void prepareParams() {
        for (const std::string& str : m_stringCache) {
            m_paramValues.push_back(str.c_str());
            m_paramLengths.push_back(str.length());
        }
    }

    inline const char* const* getParamValues() const { return &m_paramValues[0]; }
    inline const int* getParamLengths() const { return &m_paramLengths[0]; }

private:
    std::vector<std::string> m_stringCache;
    std::vector<const char*> m_paramValues;
    std::vector<int> m_paramLengths;
};

bool ELogKafkaMsgQTarget::start() {
    char hostname[128];
    char errstr[512];

    m_conf = rd_kafka_conf_new();
    if (m_clientId.empty()) {
        formatClientId();
    }

    if (rd_kafka_conf_set(m_conf, "client.id", m_clientId.c_str(), errstr, sizeof(errstr)) !=
        RD_KAFKA_CONF_OK) {
        ELogSystem::reportError("Failed to create kafka configuration object: %s", errstr);
        cleanup();
        return false;
    }

    if (rd_kafka_conf_set(m_conf, "bootstrap.servers", m_bootstrapServers.c_str(), errstr,
                          sizeof(errstr)) != RD_KAFKA_CONF_OK) {
        ELogSystem::reportError("Failed to configure kafka bootstrap servers '%s': %s",
                                m_bootstrapServers.c_str(), errstr);
        cleanup();
        return false;
    }

    m_topicConf = rd_kafka_topic_conf_new();
    if (rd_kafka_topic_conf_set(m_topicConf, "acks", "all", errstr, sizeof(errstr)) !=
        RD_KAFKA_CONF_OK) {
        ELogSystem::reportError("Failed to configure kafka topic acks=all: %s", errstr);
        cleanup();
        return false;
    }

    // create producer
    m_producer = rd_kafka_new(RD_KAFKA_PRODUCER, m_conf, errstr, sizeof(errstr));
    if (m_producer == nullptr) {
        ELogSystem::reportError("Failed to create kafka producer object: %s", errstr);
        cleanup();
        return false;
    }
    // NOTE: after successful call to rd_kafka_new(), the configuration object is no longer usable
    // and must not be destroyed, according to API documentation
    m_conf = nullptr;

    // create topic
    m_topic = rd_kafka_topic_new(m_producer, m_topicName.c_str(), m_topicConf);
    if (m_topic == nullptr) {
        ELogSystem::reportError("Failed to create kafka topic %s: %s", m_topicName.c_str(),
                                rd_kafka_err2name(rd_kafka_last_error()));
        cleanup();
        return false;
    }
    // NOTE: after successful call to rd_kafka_topic_new(), the topic configuration object is no
    // longer usable and must not be destroyed, according to API documentation
    m_topicConf = nullptr;

    return true;
}

bool ELogKafkaMsgQTarget::stop() {
    // wait for 5 seconds for all produces messages to be flushed
    uint32_t flushTimeoutMillis = m_shutdownFlushTimeoutMillis;
    if (flushTimeoutMillis == 0) {
        flushTimeoutMillis = ELOG_DEFAULT_KAFKA_SHUTDOWN_FLUSH_TIMEOUT_MILLIS;
    }
    rd_kafka_resp_err_t res = rd_kafka_flush(m_producer, flushTimeoutMillis);
    if (res != RD_KAFKA_RESP_ERR_NO_ERROR) {
        ELogSystem::reportError("Failed to flush kafka topic producer: %s", rd_kafka_err2name(res));
        return false;
    }

    // now just cleanup
    cleanup();
    return true;
}

void ELogKafkaMsgQTarget::log(const ELogRecord& logRecord) {
    if (!shouldLog(logRecord)) {
        return;
    }

    std::string logMsg;
    formatLogMsg(logRecord, logMsg);

    // unassigned partition, copy payload, no key specification, payload is formatted string
    // headers include specific log record fields
    if (rd_kafka_produce(m_topic, RD_KAFKA_PARTITION_UA, RD_KAFKA_MSG_F_COPY, (void*)logMsg.c_str(),
                         logMsg.length(), nullptr, 0, NULL) == -1) {
        const char* errMsg = rd_kafka_err2name(rd_kafka_last_error());
        ELogSystem::reportError("Failed to produce message on kafka topic %s: %s",
                                m_topicName.c_str(), errMsg);
    }
}

void ELogKafkaMsgQTarget::flush() {
    uint32_t flushTimeoutMillis = m_flushTimeoutMillis;
    if (flushTimeoutMillis == 0) {
        flushTimeoutMillis = ELOG_DEFAULT_KAFKA_FLUSH_TIMEOUT_MILLIS;
    }
    rd_kafka_resp_err_t res = rd_kafka_flush(m_producer, flushTimeoutMillis);
    if (res != RD_KAFKA_RESP_ERR_NO_ERROR) {
        ELogSystem::reportError("Failed to flush kafka topic producer: %s", rd_kafka_err2name(res));
    }
}

void ELogKafkaMsgQTarget::formatClientId() {
    std::stringstream s;
    s << getHostName() << "." << getUserName() << "." << getProgramName() << "." << getpid();
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