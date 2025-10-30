#include "rpc/elog_rpc_target.h"

#include "elog_report.h"

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogRpcTarget)

bool ELogRpcTarget::startLogTarget() {
    // a log formatter is NOT expected to be installed
    if (getLogFormatter() != nullptr) {
        ELOG_REPORT_ERROR(
            "Unexpected log format specified for RPC log target, cannot start log target");
        return false;
    }

    // create the DB formatter and install it
    m_rpcFormatter = new (std::nothrow) ELogRpcFormatter();
    if (m_rpcFormatter == nullptr) {
        ELOG_REPORT_ERROR(
            "Cannot start log target, failed to allocate RPC formatter, out of memory");
        return false;
    }
    setLogFormatter(m_rpcFormatter);
    return true;
}

}  // namespace elog