#include "elog_source.h"

#include "elog_private_logger.h"
#include "elog_shared_logger.h"

namespace elog {

ELogSource::ELogSource(ELogSourceId sourceId, const char* name, ELogSource* parent /* = nullptr */,
                       ELogLevel logLevel /* = ELEVEL_INFO */)
    : m_sourceId(sourceId),
      m_name(name),
      m_moduleName(""),
      m_parent(parent),
      m_logLevel(logLevel),
      m_logTargetAffinityMask(ELOG_ALL_TARGET_AFFINITY_MASK) {
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

    // set default module name
    if (!m_qname.empty()) {
        m_moduleName = m_qname.substr(0, m_qname.find('.'));
    }
}

ELogSource::~ELogSource() {
    for (ELogLogger* logger : m_loggers) {
        delete logger;
    }
    m_loggers.clear();
    for (auto& entry : m_children) {
        delete entry.second;
    }
}

bool ELogSource::addChild(ELogSource* logSource) {
    return m_children.insert(ChildMap::value_type(logSource->getName(), logSource)).second;
}

ELogSource* ELogSource::getChild(const char* name) {
    ELogSource* logSource = nullptr;
    ChildMap::iterator itr = m_children.find(name);
    if (itr != m_children.end()) {
        logSource = itr->second;
    }
    return logSource;
}

void ELogSource::removeChild(const char* name) {
    ChildMap::iterator itr = m_children.find(name);
    if (itr != m_children.end()) {
        m_children.erase(itr);
    }
}

void ELogSource::setLogLevel(ELogLevel logLevel, ELogPropagateMode propagateMode) {
    m_logLevel = logLevel;
    if (propagateMode == ELogPropagateMode::PM_NONE) {
        // no propagation at all
        return;
    }

    for (auto& entry : m_children) {
        ELogSource* childSource = entry.second;
        childSource->propagateLogLevel(logLevel, propagateMode);
    }
}

void ELogSource::propagateLogLevel(ELogLevel logLevel, ELogPropagateMode propagateMode) {
    // adjust self log level
    switch (propagateMode) {
        case ELogPropagateMode::PM_SET:
            m_logLevel = logLevel;
            break;

        case ELogPropagateMode::PM_RESTRICT:
            m_logLevel = std::min(m_logLevel, logLevel);
            break;

        case ELogPropagateMode::PM_LOOSE:
            m_logLevel = std::max(m_logLevel, logLevel);
            break;

        default:
            break;
    }

    // propagate to children
    for (auto& entry : m_children) {
        ELogSource* childSource = entry.second;
        childSource->propagateLogLevel(logLevel, propagateMode);
    }
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
