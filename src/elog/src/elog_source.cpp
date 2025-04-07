#include "elog_source.h"

#include "elog_logger.h"

namespace elog {

ELogSource::~ELogSource() {
    for (ELogLogger* logger : m_loggers) {
        delete logger;
    }
    m_loggers.clear();
}

ELogLogger* ELogSource::createLogger() {
    ELogLogger* logger = new (std::nothrow) ELogLogger(this);
    if (logger != nullptr) {
        m_loggers.insert(logger);
    }
    return logger;
}

}  // namespace elog
