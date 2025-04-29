#include "elog_source.h"

#include "elog_private_logger.h"
#include "elog_shared_logger.h"

namespace elog {

ELogSource::ELogSource(ELogSourceId sourceId, const char* name, ELogSource* parent /* = nullptr */,
                       ELogLevel logLevel /* = ELEVEL_INFO */)
    : m_sourceId(sourceId),
      m_name(name),
      m_moduleName(name),
      m_parent(parent),
      m_logLevel(logLevel) {
    if (parent != nullptr) {
        const char* parentQName = parent->getQualifiedName();
        if (*parentQName == 0) {
            m_qname = name;
        } else {
            m_qname = std::string(parentQName) + "." + name;
        }
    } else {
        m_qname = name;
    }
}

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
