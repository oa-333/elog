#include "net/elog_net_target_provider.h"

#ifdef ELOG_ENABLE_NET

#include <transport/tcp_client.h>
#include <transport/udp_client.h>

#include "elog_common.h"
#include "elog_config_loader.h"
#include "elog_config_parser.h"
#include "elog_report.h"
#include "msg/elog_msg_config_loader.h"
#include "msg/elog_msg_internal.h"
#include "msg/elog_msg_target.h"

// TODO: change elog_grpc namespace to elog_proto

// TODO: all formatters must be registered with name in a factory so that we can use the special
// format configuration syntax as follows:
//
//      log_format=<type_name>:<spec>
//
// for instance:
//
//      log_format=list:{${time}, ${msg}, ...}
//
// this allows using common target load configuration code, just using a factor.
// in addition, the rpc log formatter should be rename ELogListFormatter and put in common code.

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogNetTargetProvider)

ELogTarget* ELogNetTargetProvider::loadTarget(const ELogConfigMapNode* logTargetCfg) {
    // expected url is as follows:
    // net://[tcp/udp]?mode=[sync/async]&
    //  address=host:port&
    //  log_format=rpc:{<log record field list>}& (temporary syntax, pending refactor)
    //  binary_format={protobuf/thrift/avro}&
    //  compress=value&
    //  max_concurrent_requests=value&
    //  connect_timeout=value&
    //  write_timeout=value&
    //  read_timeout=value&
    //  resend_period=value&
    //  backlog_limit=value&
    //  shutdown_timeout=value
    //  shutdown_polling_timeout=value

    // first verify self type
    if (m_type.compare("tcp") != 0 && m_type.compare("udp") != 0) {
        ELOG_REPORT_ERROR("Invalid net log target specification, unsupported transport type '%s'",
                          m_type.c_str());
        return nullptr;
    }

    // we expect at most 9 properties, of which only mode, address and log_format are mandatory
    // log record aggregation may be controlled by flush policy
    std::string mode;
    if (!ELogConfigLoader::getLogTargetStringProperty(logTargetCfg, "net", "mode", mode)) {
        return nullptr;
    }

    bool sync = false;
    if (mode.compare("sync") == 0) {
        sync = true;
    } else if (mode.compare("async") != 0) {
        ELOG_REPORT_ERROR(
            "Invalid net log target specification, unsupported property 'mode' value '%s' "
            "(context: %s)",
            mode.c_str(), logTargetCfg->getFullContext());
        return nullptr;
    }

    // compression flag
    bool compress = false;
    if (!ELogConfigLoader::getOptionalLogTargetBoolProperty(logTargetCfg, "net", "compress",
                                                            compress)) {
        return nullptr;
    }

    // maximum concurrent requests
    uint32_t maxConcurrentRequests = 0;
    if (!ELogConfigLoader::getLogTargetUInt32Property(
            logTargetCfg, "net", "max_concurrent_requests", maxConcurrentRequests)) {
        return nullptr;
    }
    // TODO: verify legal range for this value

    // binary format
    std::string binaryFormat = ELOG_DEFAULT_MSG_BINARY_FORMAT;
    if (!ELogConfigLoader::getOptionalLogTargetStringProperty(logTargetCfg, "net", "binary_format",
                                                              binaryFormat)) {
        return nullptr;
    }
    ELogBinaryFormatProvider* binaryFormatProvider =
        constructBinaryFormatProvider(binaryFormat.c_str(), commutil::ByteOrder::NETWORK_ORDER);
    if (binaryFormatProvider == nullptr) {
        ELOG_REPORT_ERROR(
            "Invalid net log target specification, unsupported binary format '%s' (context: %s)",
            binaryFormat.c_str(), logTargetCfg->getFullContext());
        return nullptr;
    }

    // load common transport configuration (use defaults, see ELogMsgConfig default constructor)
    commutil::MsgConfig msgConfig;
    if (!ELogMsgConfigLoader::loadMsgConfig(logTargetCfg, "net", msgConfig)) {
        ELOG_REPORT_ERROR(
            "Invalid net log target specification, invalid transport properties (context: %s)",
            logTargetCfg->getFullContext());
        delete binaryFormatProvider;
        return nullptr;
    }

    std::string address;
    if (!ELogConfigLoader::getLogTargetStringProperty(logTargetCfg, "net", "address", address)) {
        delete binaryFormatProvider;
        return nullptr;
    }

    std::string host;
    int port = 0;
    if (!ELogConfigParser::parseHostPort(address, host, port)) {
        ELOG_REPORT_ERROR(
            "Invalid net log target specification, failed to parse address %s (context: %s)",
            address.c_str(), logTargetCfg->getFullContext());
        delete binaryFormatProvider;
        return nullptr;
    }

    commutil::DataClient* dataClient = nullptr;
    if (m_type.compare("tcp") == 0) {
        dataClient = new (std::nothrow)
            commutil::TcpClient(host.c_str(), port, msgConfig.m_connectTimeoutMillis);
    } else if (m_type.compare("udp") == 0) {
        dataClient = new (std::nothrow) commutil::UdpClient(host.c_str(), port);
    } else {
        ELOG_REPORT_ERROR(
            "Failed to load net log target, invalid transport type '%s' (internal error)",
            m_type.c_str());
        delete binaryFormatProvider;
        return nullptr;
    }

    ELogMsgTarget* target = new (std::nothrow) ELogMsgTarget(
        "net", msgConfig, dataClient, binaryFormatProvider, sync, compress, maxConcurrentRequests);
    if (target == nullptr) {
        ELOG_REPORT_ERROR("Failed to allocate net log target, out of memory");
    }
    return target;
}

}  // namespace elog

#endif  // ELOG_ENABLE_NET
