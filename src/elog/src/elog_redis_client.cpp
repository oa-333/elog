#include "elog_redis_client.h"

#ifdef ELOG_USING_REDIS

#include <cassert>

#include "elog_common.h"
#include "elog_report.h"

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogRedisClient)

bool ELogRedisClient::connectRedis() {
    // initialize SSL context if needed
    if (m_usingSSL && m_sslContext == nullptr) {
        redisSSLContextError sslError;
        m_sslContext = redisCreateSSLContextWithOptions(&m_sslOptions, &sslError);
        if (m_sslContext == nullptr) {
            ELOG_REPORT_ERROR("Failed to initialize SSL options for Redis client: %s",
                              redisSSLContextGetError(sslError));
            return false;
        }
    }

    // try the servers one by one
    m_currentServer = 0;
    for (const auto& pair : m_serverList) {
        // connect to database
        const std::string host = pair.first;
        int port = pair.second;
        ELOG_REPORT_TRACE("Attempting to connect to redis server at %s:%d", host.c_str(), port);
        m_redisContext = redisConnect(host.c_str(), port);
        if (m_redisContext == nullptr) {
            ELOG_REPORT_WARN("Failed to open Redis db connection to %s:%d", host.c_str(), port);
            ++m_currentServer;
            continue;
        }
        if (m_redisContext->err != 0) {
            if (m_redisContext->err == REDIS_ERR_IO) {
                int errNum = errno;
                ELOG_REPORT_WARN("Failed to open Redis db connection to %s:%d: %s", host.c_str(),
                                 port, ELogReport::sysErrorToStr(errNum));
            } else {
                ELOG_REPORT_WARN("Failed to open Redis db connection to %s:%d: %s", host.c_str(),
                                 port, m_redisContext->errstr);
            }
            redisFree(m_redisContext);
            m_redisContext = nullptr;
            continue;
        }
        ELOG_REPORT_TRACE("Connected to Redis at %s:%d", host.c_str(), port);

        if (!m_password.empty()) {
            redisReply* reply =
                (redisReply*)redisCommand(m_redisContext, "AUTH %s", m_password.c_str());
            if (reply == NULL || reply->type == REDIS_REPLY_ERROR) {
                // SECURITY NOTE: DO NOT PRINT PASSWORD IN ERROR MESSAGE!!
                ELOG_REPORT_ERROR("Redis authentication error: %s", m_redisContext->errstr);
                if (reply != nullptr) {
                    freeReplyObject(reply);
                }
                // no sense in keep trying, the password is wrong
                disconnectRedis();
                return false;
            }
            // success
            freeReplyObject(reply);
        } else if (m_usingSSL) {
            int res = redisInitiateSSLWithContext(m_redisContext, m_sslContext);
            if (res != 0) {
                ELOG_REPORT_ERROR("Failed to initialize SSL on Redis connection (error code: %d)",
                                  res);
                // no sense in keep trying, the password is wrong
                disconnectRedis();
                return false;
            }
        }
        m_connectionReady = true;
        return true;
    }

    m_currentServer = (uint32_t)-1;
    return false;
}

void ELogRedisClient::disconnectRedis() {
    if (m_redisContext != nullptr) {
        redisFree(m_redisContext);
        m_currentServer = (uint32_t)-1;
        m_redisContext = nullptr;
    }
    if (m_sslContext != nullptr) {
        redisFreeSSLContext(m_sslContext);
        m_sslContext = nullptr;
    }
    m_connectionReady = false;
}

bool ELogRedisClient::isRedisConnected() { return m_redisContext != nullptr && m_connectionReady; }

bool ELogRedisClient::executeRedisCommand(const char* cmd) {
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
        ELOG_REPORT_ERROR("Failed to allocate Redis argument array of size %zu", argc);
        return false;
    }
    for (uint32_t i = 0; i < argc; ++i) {
        argv[i] = cmdTokens[i].c_str();
    }

    // execute command
    redisReply* reply = (redisReply*)redisCommandArgv(m_redisContext, (int)argc, argv, argvlen);
    res = checkReply(reply);  // no specified expected reply type
    if (!res) {
        ELOG_REPORT_ERROR("Failed to execute Redis command: '%s'", cmd);
    }

    // clean up and return result
    if (reply != nullptr) {
        freeReplyObject(reply);
    }
    delete[] argv;

    // check if connection should be cleaned up
    if (!m_connectionReady) {
        disconnectRedis();
    }
    return res;
}

bool ELogRedisClient::checkReply(redisReply* reply, int expectedType) {
    // check for null reply
    if (reply == nullptr) {
        // check if connection aborted/reset
        // NOTE: we avoid side-effects, and just mark the connection as unusable
        if (m_redisContext->err != 0) {
            if (m_redisContext->err == REDIS_ERR_IO) {
                int errNum = errno;
                ELOG_REPORT_WARN(
                    "Redis context indicates error: %s (system error: %s, error code: %d):",
                    m_redisContext->errstr, errNum, ELogReport::sysErrorToStr(errNum));
                m_connectionReady = false;
            } else if (m_redisContext->err == REDIS_ERR_EOF) {
                ELOG_REPORT_WARN("Redis context indicates connection closed: %s",
                                 m_redisContext->errstr);
                m_connectionReady = false;
            } else {
                ELOG_REPORT_WARN("Redis context indicates error: %s", m_redisContext->errstr);
                // all other errors not necessarily indicate connection broken
            }
        } else {
            ELOG_REPORT_ERROR("Received null reply from Redis, without specific error");
            // not disconnecting yet
        }
        return false;
    }
    // check for error
    else if (reply->type == REDIS_REPLY_ERROR) {
        ELOG_REPORT_ERROR("Receive error from Redis server: %s", reply->str);
        return false;
    }
    // check for unexpected result type
    else if (expectedType != -1 && reply->type != expectedType) {
        ELOG_REPORT_ERROR(
            "Receive reply from Redis server with unexpected type, expecting %d, got %d",
            expectedType, reply->type);
        return false;
    }
    return true;
}

void ELogRedisClient::mergeQuotedTokens(const std::vector<std::string>& tokens,
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

bool ELogRedisClient::processReply(redisReply* reply) {
    bool res = true;
    if (reply == NULL || reply->type == REDIS_REPLY_ERROR) {
        ELOG_REPORT_ERROR("Failed to execute Redis command: %s (%d)",
                          reply != nullptr ? reply->str : m_redisContext->errstr,
                          m_redisContext->err);
        res = false;
    }
    if (reply != nullptr) {
        freeReplyObject(reply);
    }
    if (!m_connectionReady) {
        disconnectRedis();
    }
    return res;
}

}  // namespace elog

#endif  // ELOG_USING_REDIS