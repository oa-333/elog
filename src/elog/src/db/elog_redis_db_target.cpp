#include "db/elog_redis_db_target.h"

#ifdef ELOG_ENABLE_REDIS_DB_CONNECTOR

#include <cstring>
#include <sstream>

#include "elog_buffer_receptor.h"
#include "elog_common.h"
#include "elog_report.h"

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogRedisDbTarget)

ELOG_IMPLEMENT_LOG_TARGET(ELogRedisDbTarget)

// NOTE: If Redis API allows in the future, we can use this receptor
#if 0
class ELogRedisDbFieldReceptor : public ELogFieldReceptor {
public:
    ELogRedisDbFieldReceptor() {}
    ELogRedisDbFieldReceptor(const ELogRedisDbFieldReceptor&) = delete;
    ELogRedisDbFieldReceptor(ELogRedisDbFieldReceptor&&) = delete;
    ELogRedisDbFieldReceptor& operator=(const ELogRedisDbFieldReceptor&) = delete;
    ~ELogRedisDbFieldReceptor() final {}

    /** @brief Receives a string log record field. */
    void receiveStringField(uint32_t typeId, const char* field, const ELogFieldSpec& fieldSpec,
                            size_t length) final {
        m_stringCache.push_back(field);
    }

    /** @brief Receives an integer log record field. */
    void receiveIntField(uint32_t typeId, uint64_t field, const ELogFieldSpec& fieldSpec) final {
        m_stringCache.push_back(std::to_string(field));
    }

    /** @brief Receives a time log record field. */
    void receiveTimeField(uint32_t typeId, const ELogTime& logTime, const char* timeStr,
                          const ELogFieldSpec& fieldSpec, size_t length) final {
        m_stringCache.push_back(timeStr);
    }

    /** @brief Receives a log level log record field. */
    void receiveLogLevelField(uint32_t typeId, ELogLevel logLevel,
                              const ELogFieldSpec& fieldSpec) final {
        const char* logLevelStr = elogLevelToStr(logLevel);
        m_stringCache.push_back(logLevelStr);
    }

    void prepareParams() {
        for (const std::string& str : m_stringCache) {
            m_paramValues.push_back(str.c_str());
            m_paramLengths.push_back(str.length());
        }
    }

    inline const char** getParamValues() const { return (const char**)&m_paramValues[0]; }
    inline size_t getParamCount() const { return m_paramValues.size(); }
    inline const size_t* getParamLengths() const { return &m_paramLengths[0]; }

private:
    std::vector<std::string> m_stringCache;
    std::vector<const char*> m_paramValues;
    std::vector<size_t> m_paramLengths;
};
#endif

bool ELogRedisDbTarget::initDbTarget() {
    for (const std::string& indexInsert : m_indexInserts) {
        ELOG_REPORT_TRACE("Parsing index insert: %s", indexInsert.c_str());
        ELogDbFormatter* formatter = new (std::nothrow) ELogDbFormatter();
        if (formatter == nullptr) {
            ELOG_REPORT_ERROR(
                "Failed to allocate formatter for Redis index insert statement, out of memory");
            termDbTarget();
            return false;
        }
        formatter->setQueryStyle(ELogDbFormatter::QueryStyle::QS_PRINTF);
        if (!formatter->initialize(indexInsert.c_str())) {
            ELOG_REPORT_ERROR("Failed to parse Redis index insert statement: %s",
                              indexInsert.c_str());
            destroyLogFormatter(formatter);
            termDbTarget();
            return false;
        }
        m_indexStmtFormatters.push_back(formatter);
    }
    return true;
}

void ELogRedisDbTarget::termDbTarget() {
    for (ELogDbFormatter* formatter : m_indexStmtFormatters) {
        if (formatter != nullptr) {
            destroyLogFormatter(formatter);
        }
    }
    m_indexStmtFormatters.clear();
}

bool ELogRedisDbTarget::connectDb(void* dbData) {
    RedisDbData* redisDbData = validateConnectionState(dbData, false);
    if (redisDbData == nullptr) {
        return false;
    }

    // connect to database
    redisDbData->m_context = redisConnect(m_host.c_str(), m_port);
    if (redisDbData->m_context == nullptr) {
        ELOG_REPORT_MODERATE_ERROR_DEFAULT("Failed to open Redis db connection to %s:%d: %s",
                                           m_host.c_str(), m_port);
        return false;
    }
    ELOG_REPORT_TRACE("Connected to Redis");

    if (!m_passwd.empty()) {
        redisReply* reply =
            (redisReply*)redisCommand(redisDbData->m_context, "AUTH %s", m_passwd.c_str());
        if (reply == NULL || reply->type == REDIS_REPLY_ERROR) {
            ELOG_REPORT_MODERATE_ERROR_DEFAULT("Redis authentication error: %s",
                                               redisDbData->m_context->errstr);
            if (reply != nullptr) {
                freeReplyObject(reply);
            }
            redisFree(redisDbData->m_context);
            redisDbData->m_context = nullptr;
            return false;
        }
        freeReplyObject(reply);
    }

    // no need to prepare any statement,
    // the formatter and receptor each time prepare a redis string command
    return true;
}

bool ELogRedisDbTarget::disconnectDb(void* dbData) {
    RedisDbData* redisDbData = validateConnectionState(dbData, true);
    if (redisDbData == nullptr) {
        return false;
    }

    redisFree(redisDbData->m_context);
    redisDbData->m_context = nullptr;
    return true;
}

bool ELogRedisDbTarget::execInsert(const ELogRecord& logRecord, void* dbData,
                                   uint64_t& bytesWritten) {
    RedisDbData* redisDbData = validateConnectionState(dbData, true);
    if (redisDbData == nullptr) {
        return false;
    }

    // NOTE: due to redis API we need to re-parse the processed insert statement, then separate into
    // tokens, then call redisCommandArgv(), otherwise we get an error reply (apparently the
    // redisCommand() call requires all those %s for computing argument count)
    // this is true also for the index update statement(s)

    // this puts each log record field into the correct place in the prepared statement parameters
    ELogBuffer buffer;
    ELogBufferReceptor redisFieldReceptor(buffer);
    fillInsertStatement(logRecord, &redisFieldReceptor);
    redisFieldReceptor.finalize();

    // execute the main insert statement
    if (!executeRedisCommand(redisDbData, redisFieldReceptor.getBuffer())) {
        return false;
    }
    bytesWritten = redisFieldReceptor.getBufferSize();

    // now execute additional "index" statements
    for (uint32_t i = 0; i < m_indexStmtFormatters.size(); ++i) {
        ELogBuffer indexBuffer;
        ELogBufferReceptor indexReceptor(indexBuffer);
        m_indexStmtFormatters[i]->fillInsertStatement(logRecord, &indexReceptor);
        indexReceptor.finalize();

        if (!executeRedisCommand(redisDbData, indexReceptor.getBuffer())) {
            ELOG_REPORT_MODERATE_ERROR_DEFAULT("Failed to execute Redis index insert command: %s",
                                               indexReceptor.getBuffer());
            return false;
        }
        bytesWritten += indexReceptor.getBufferSize();
    }
    return true;
}

ELogRedisDbTarget::RedisDbData* ELogRedisDbTarget::validateConnectionState(void* dbData,
                                                                           bool shouldBeConnected) {
    if (dbData == nullptr) {
        ELOG_REPORT_MODERATE_ERROR_DEFAULT(
            "Cannot connect to Redis database, invalid connection state (internal error, database "
            "object is null)");
        return nullptr;
    }
    RedisDbData* redisDbData = (RedisDbData*)dbData;
    if (shouldBeConnected && redisDbData->m_context == nullptr) {
        ELOG_REPORT_MODERATE_ERROR_DEFAULT(
            "Cannot connect to Redis database, invalid connection state (internal error, Redis "
            "connection is null)");
        return nullptr;
    } else if (!shouldBeConnected && redisDbData->m_context != nullptr) {
        ELOG_REPORT_MODERATE_ERROR_DEFAULT(
            "Cannot connect to Redis database, invalid connection state (internal error, Redis "
            "connection is NOT null as expected)");
        return nullptr;
    }
    return redisDbData;
}

bool ELogRedisDbTarget::executeRedisCommand(RedisDbData* redisDbData, const char* cmd) {
    ELOG_REPORT_TRACE("Executing redis command: %s", cmd);
    std::vector<std::string> tokens;
    tokenize(cmd, tokens);

    // we need to merge tokens that start with a quote until we find a token that ends with a quote
    std::vector<std::string> cmdTokens;
    mergeQuotedTokens(tokens, cmdTokens);

    // prepare parameter length array
    std::vector<size_t> paramLengths;
    for (const std::string& str : cmdTokens) {
        paramLengths.push_back(str.length());
    }
    const size_t* argvlen = &paramLengths[0];

    // convert command tokens into raw string array
    bool res = true;
    size_t argc = cmdTokens.size();
    const char** argv = new (std::nothrow) const char*[argc];
    if (argv == nullptr) {
        ELOG_REPORT_MODERATE_ERROR_DEFAULT("Failed to allocate Redis argument array of size %zu",
                                           argc);
        return false;
    }
    for (uint32_t i = 0; i < argc; ++i) {
        argv[i] = cmdTokens[i].c_str();
    }

    // execute command
    redisReply* reply =
        (redisReply*)redisCommandArgv(redisDbData->m_context, (int)argc, argv, argvlen);
    if (reply == nullptr || reply->type == REDIS_REPLY_ERROR) {
        const char* errStr = reply ? (const char*)reply->str : "N/A";
        ELOG_REPORT_MODERATE_ERROR_DEFAULT("Failed to execute Redis command '%s': %s", cmd, errStr);
        res = false;
    }

    // clean up and return result
    if (reply != nullptr) {
        freeReplyObject(reply);
    }
    delete[] argv;
    return res;
}

void ELogRedisDbTarget::mergeQuotedTokens(const std::vector<std::string>& tokens,
                                          std::vector<std::string>& cmdTokens) {
    std::string tokenSpan;
    bool isInTokenSpan = false;
    for (const std::string& token : tokens) {
        if (!isInTokenSpan) {
            if (token[0] == '\"' && token.back() != '\"') {
                tokenSpan = token;
                isInTokenSpan = true;
            } else {
                // strip enclosing quotes if found
                if (token[0] == '\"' && token.back() == '\"') {
                    cmdTokens.push_back(token.substr(1, token.size() - 2));
                } else {
                    cmdTokens.push_back(token);
                }
            }
        } else {
            tokenSpan += " ";
            tokenSpan += token;
            if (token.back() == '\"') {
                assert(token[0] != '\"');
                // strip enclosing quotes
                cmdTokens.push_back(tokenSpan.substr(1, tokenSpan.size() - 2));
                isInTokenSpan = false;
            }
            // otherwise keep accumulating and stay in this state
        }
    }
}

}  // namespace elog

#endif  // ELOG_ENABLE_PGSQL_DB_CONNECTOR