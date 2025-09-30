#include "ipc/elog_pipe_target_provider.h"

#ifdef ELOG_ENABLE_IPC

#include <transport/pipe_client.h>

#include "elog_common.h"
#include "elog_config_loader.h"
#include "elog_config_parser.h"
#include "elog_report.h"
#include "msg/elog_binary_format_provider.h"
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

ELOG_DECLARE_REPORT_LOGGER(ELogPipeTargetProvider)

ELogTarget* ELogPipeTargetProvider::loadTarget(const ELogConfigMapNode* logTargetCfg) {
    // expected url is as follows:
    // ipc://pipe?mode=[sync/async]&
    //  address=pipeName&
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
    if (m_type.compare("pipe") != 0) {
        ELOG_REPORT_ERROR("Invalid pipe log target specification, unsupported transport type '%s'",
                          m_type.c_str());
        return nullptr;
    }

    // load common messaging configuration
    ELogMsgConfig msgConfig;
    if (!ELogMsgConfigLoader::loadMsgConfig(logTargetCfg, "ipc", msgConfig)) {
        ELOG_REPORT_ERROR("Failed to load pipe target configuration");
        return nullptr;
    }

    std::string pipeName;
    if (!ELogConfigLoader::getLogTargetStringProperty(logTargetCfg, "ipc", "address", pipeName)) {
        delete msgConfig.m_binaryFormatProvider;
        return nullptr;
    }

    commutil::DataClient* dataClient = new (std::nothrow)
        commutil::PipeClient(pipeName.c_str(), msgConfig.m_commConfig.m_connectTimeoutMillis);
    if (dataClient == nullptr) {
        ELOG_REPORT_ERROR("Failed to allocate data client, out of memory");
        delete msgConfig.m_binaryFormatProvider;
        return nullptr;
    }

    ELogMsgTarget* target = new (std::nothrow) ELogMsgTarget("pipe", msgConfig, dataClient);
    if (target == nullptr) {
        ELOG_REPORT_ERROR("Failed to allocate pipe log target, out of memory");
        delete dataClient;
        delete msgConfig.m_binaryFormatProvider;
    }
    return target;
}

}  // namespace elog

#endif  // ELOG_ENABLE_IPC
