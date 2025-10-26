#include "msgq/elog_msgq_target.h"

#include "elog_report.h"

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogMsgQTarget)

bool ELogMsgQTarget::startLogTarget() {
    m_formatter = ELogPropsFormatter::create();
    if (m_formatter == nullptr) {
        ELOG_REPORT_ERROR("Failed to create properties formatter, out of memory");
        return false;
    }
    return true;
}

bool ELogMsgQTarget::stopLogTarget() {
    if (m_formatter != nullptr) {
        ELogPropsFormatter::destroy(m_formatter);
        m_formatter = nullptr;
    }
    return true;
}

}  // namespace elog
