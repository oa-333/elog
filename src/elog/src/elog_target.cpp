#include "elog_target.h"

#include "elog_flush_policy.h"
#include "elog_system.h"

namespace elog {

bool ELogCombinedTarget::start() {
    for (ELogTarget* target : m_logTargets) {
        if (!target->start()) {
            return false;
        }
    }
    return true;
}

bool ELogCombinedTarget::stop() {
    for (ELogTarget* target : m_logTargets) {
        if (!target->stop()) {
            return false;
        }
    }
    return true;
}

void ELogCombinedTarget::log(const ELogRecord& logRecord) {
    for (ELogTarget* target : m_logTargets) {
        target->log(logRecord);
    }
}

void ELogCombinedTarget::flush() {
    for (ELogTarget* target : m_logTargets) {
        target->flush();
    }
}

void ELogAbstractTarget::log(const ELogRecord& logRecord) {
    // partially implement logging - filter and format message
    // this might not suite all targets, as formatting might take place on a later phase
    if (ELogSystem::filterLogMsg(logRecord)) {
        std::string logMsg;
        ELogSystem::formatLogMsg(logRecord, logMsg);
        log(logMsg.c_str());
        if (m_flushPolicy->shouldFlush(logMsg.length())) {
            flush();
        }
    }
}

}  // namespace elog
