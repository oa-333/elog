#ifndef __ELOG_KAFKA_MSGQ_TARGET_H__
#define __ELOG_KAFKA_MSGQ_TARGET_H__

#include "elog_def.h"

#ifdef ELOG_ENABLE_KAFKA_MSGQ_CONNECTOR

#include <librdkafka/rdkafka.h>

#include "elog_msgq_target.h"

namespace elog {

class ELogKafkaMsgQTarget : public ELogMsgQTarget {
public:
    ELogKafkaMsgQTarget(const std::string& bootstrapServers, const std::string& topicName,
                        const std::string& headers, int partition = -1,
                        uint32_t flushTimeoutMillis = 0, uint32_t shutdownFlushTimeoutMillis = 0)
        : m_bootstrapServers(bootstrapServers),
          m_topicName(topicName),
          m_headers(headers),
          m_partition(partition),
          m_flushTimeoutMillis(flushTimeoutMillis),
          m_shutdownFlushTimeoutMillis(shutdownFlushTimeoutMillis),
          m_conf(nullptr),
          m_topicConf(nullptr),
          m_producer(nullptr),
          m_topic(nullptr) {}

    ELogKafkaMsgQTarget(const ELogKafkaMsgQTarget&) = delete;
    ELogKafkaMsgQTarget(ELogKafkaMsgQTarget&&) = delete;
    ~ELogKafkaMsgQTarget() final {}

    /** @brief Sends a log record to a log target. */
    void log(const ELogRecord& logRecord) final;

    /** @brief Orders a buffered log target to flush it log messages. */
    void flush() final;

protected:
    /** @brief Order the log target to start (required for threaded targets). */
    bool startLogTarget() final;

    /** @brief Order the log target to stop (required for threaded targets). */
    bool stopLogTarget() final;

private:
    std::string m_bootstrapServers;
    std::string m_topicName;
    std::string m_headers;
    int m_partition;
    uint32_t m_flushTimeoutMillis;
    uint32_t m_shutdownFlushTimeoutMillis;

    std::string m_clientId;
    rd_kafka_conf_t* m_conf;
    rd_kafka_topic_conf_t* m_topicConf;
    rd_kafka_t* m_producer;
    rd_kafka_topic_t* m_topic;

    void formatClientId();

    // void convertToPgParamTypes();
    void formatConnString(const std::string& host, uint32_t port, const std::string& db,
                          const std::string& user, const std::string& passwd);

    void cleanup();
};

}  // namespace elog

#endif  // ELOG_ENABLE_KAFKA_MSGQ_CONNECTOR

#endif  // __ELOG_KAFKA_MSGQ_TARGET_H__