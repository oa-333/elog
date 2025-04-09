#include "elog_source.h"

#include "elog_private_logger.h"
#include "elog_shared_logger.h"

namespace elog {

ELogSource::~ELogSource() {
    for (ELogLogger* logger : m_loggers) {
        delete logger;
    }
    m_loggers.clear();
}

ELogLogger* ELogSource::createSharedLogger() {
    ELogLogger* logger = new (std::nothrow) ELogSharedLogger(this);
    if (logger != nullptr) {
        m_loggers.insert(logger);
    }
    return logger;
}

ELogLogger* ELogSource::createPrivateLogger() {
    ELogLogger* logger = new (std::nothrow) ELogPrivateLogger(this);
    if (logger != nullptr) {
        m_loggers.insert(logger);
    }
    return logger;
}

}  // namespace elog
