#ifndef __ELOG_RPC_TARGET_H__
#define __ELOG_RPC_TARGET_H__

#include <condition_variable>
#include <mutex>
#include <thread>

#include "elog_rpc_formatter.h"
#include "elog_target.h"

namespace elog {

/** @brief Abstract parent class for message queue log targets. */
class ELOG_API ELogRpcTarget : public ELogTarget {
protected:
    ELogRpcTarget(const char* server, const char* host, int port, const char* functionName)
        : ELogTarget("rpc"),
          m_server(server),
          m_host(host),
          m_port(port),
          m_functionName(functionName) {}
    ELogRpcTarget(const ELogRpcTarget&) = delete;
    ELogRpcTarget(ELogRpcTarget&&) = delete;
    ELogRpcTarget& operator=(const ELogRpcTarget&) = delete;
    ~ELogRpcTarget() override {}

    /** @brief Orders a buffered log target to flush it log messages. */
    bool flushLogTarget() override { return true; }

    /**
     * @brief Parses the parameters loaded from configuration and builds all log record field
     * selectors.
     * @param params The parameters to parse.
     * @return true If succeeded, otherwise false.
     */
    inline bool parseParams(const std::string& params) {
        return m_rpcFormatter.parseParams(params);
    }

    /**
     * @brief Applies all field selectors to the given log record, so that all headers are filled.
     * @param logRecord The log record to process.
     * @param receptor The receptor that receives log record fields and transfers them to the
     * message headers.
     */
    inline void fillInParams(const elog::ELogRecord& logRecord, elog::ELogFieldReceptor* receptor) {
        m_rpcFormatter.fillInParams(logRecord, receptor);
    }

protected:
    std::string m_server;
    std::string m_host;
    int m_port;
    std::string m_functionName;

    inline ELogRpcFormatter* getRpcFormatter() { return &m_rpcFormatter; }

private:
    ELogRpcFormatter m_rpcFormatter;
};

}  // namespace elog

#endif  // __ELOG_RPC_TARGET_H__