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
    //  log_format=msg:<comma-based log record field list>&
    //  binary_format={protobuf/thrift/avro}&
    //  compress=value&
    //  max_concurrent_requests=value&
    //  connect_timeout=value&
    //  send_timeout=value&
    //  resend_period=value&
    //  expire_timeout=value&
    //  backlog_limit=value&
    //  shutdown_timeout=value&
    //  shutdown_polling_timeout=value

    // first verify self type
    if (m_type.compare("tcp") != 0 && m_type.compare("udp") != 0) {
        ELOG_REPORT_ERROR("Invalid net log target specification, unsupported transport type '%s'",
                          m_type.c_str());
        return nullptr;
    }

    // load common messaging configuration
    ELogMsgConfig msgConfig;
    if (!ELogMsgConfigLoader::loadMsgConfig(logTargetCfg, "net", msgConfig)) {
        ELOG_REPORT_ERROR("Failed to load net target configuration");
        return nullptr;
    }

    // load address
    std::string address;
    if (!ELogConfigLoader::getLogTargetStringProperty(logTargetCfg, "net", "address", address)) {
        delete msgConfig.m_binaryFormatProvider;
        return nullptr;
    }

    // parse address to host port
    std::string host;
    int port = 0;
    if (!ELogConfigParser::parseHostPort(address, host, port)) {
        ELOG_REPORT_ERROR(
            "Invalid net log target specification, failed to parse address %s (context: %s)",
            address.c_str(), logTargetCfg->getFullContext());
        delete msgConfig.m_binaryFormatProvider;
        return nullptr;
    }

    // create the data client
    commutil::DataClient* dataClient = nullptr;
    if (m_type.compare("tcp") == 0) {
        dataClient = new (std::nothrow)
            commutil::TcpClient(host.c_str(), port, msgConfig.m_commConfig.m_connectTimeoutMillis);
    } else if (m_type.compare("udp") == 0) {
        dataClient = new (std::nothrow) commutil::UdpClient(host.c_str(), port);
    } else {
        ELOG_REPORT_ERROR(
            "Failed to load net log target, invalid transport type '%s' (internal error)",
            m_type.c_str());
        delete msgConfig.m_binaryFormatProvider;
        return nullptr;
    }
    if (dataClient == nullptr) {
        ELOG_REPORT_ERROR("Failed to allocate data client, out of memory");
        delete msgConfig.m_binaryFormatProvider;
        return nullptr;
    }

    ELogMsgTarget* target = new (std::nothrow) ELogMsgTarget("net", msgConfig, dataClient);
    if (target == nullptr) {
        ELOG_REPORT_ERROR("Failed to allocate net log target, out of memory");
        delete dataClient;
        delete msgConfig.m_binaryFormatProvider;
    }
    return target;
}

}  // namespace elog

#endif  // ELOG_ENABLE_NET
