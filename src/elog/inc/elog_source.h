#ifndef __ELOG_SOURCE_H__
#define __ELOG_SOURCE_H__

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "elog_level.h"

namespace elog {

class ELogLogger;

typedef uint32_t ELogSourceId;

#define ELOG_INVALID_SOURCE_ID ((ELogSourceId) - 1)

class ELogSource {
public:
    ELogSource(const ELogSource&) = delete;
    ELogSource(ELogSource&&) = delete;

    // log source name/id
    inline ELogSourceId getId() const { return m_sourceId; }
    inline const char* getName() const { return m_name.c_str(); }
    inline const char* getQualifiedName() const { return m_qname.c_str(); }

    // module name
    inline void setModuleName(const char* moduleName) { m_moduleName = moduleName; }
    inline const char* getModuleName() const { return m_moduleName.c_str(); }

    // navigation
    inline const ELogSource* getParent() const { return m_parent; }

    inline bool addChild(ELogSource* logSource) {
        return m_children.insert(ChildMap::value_type(logSource->getName(), logSource)).second;
    }

    inline ELogSource* getChild(const char* name) {
        ELogSource* logSource = nullptr;
        ChildMap::iterator itr = m_children.find(name);
        if (itr != m_children.end()) {
            logSource = itr->second;
        }
        return logSource;
    }

    inline void removeChild(const char* name) {
        ELogSource* logSource = nullptr;
        ChildMap::iterator itr = m_children.find(name);
        if (itr != m_children.end()) {
            m_children.erase(itr);
        }
    }

    // log level
    inline void setLogLevel(ELogLevel logLevel) { m_logLevel = logLevel; }
    inline bool canLog(ELogLevel logLevel) {
        return static_cast<int>(logLevel) <= static_cast<int>(m_logLevel);
    }

    // logger interface
    ELogLogger* createLogger();

private:
    ELogSourceId m_sourceId;
    std::string m_name;
    std::string m_qname;
    std::string m_moduleName;
    ELogSource* m_parent;
    ELogLevel m_logLevel;
    typedef std::unordered_map<std::string, ELogSource*> ChildMap;
    ChildMap m_children;
    std::unordered_set<ELogLogger*> m_loggers;

    ELogSource(ELogSourceId sourceId, const char* name, ELogSource* parent = nullptr,
               ELogLevel logLevel = ELEVEL_INFO)
        : m_sourceId(sourceId),
          m_name(name),
          m_moduleName(name),
          m_parent(parent),
          m_logLevel(logLevel) {
        m_qname = parent ? (std::string(parent->getQualifiedName()) + "." + name) : name;
    }
    ~ELogSource();

    friend class ELogSystem;
};

}  // namespace elog

#endif  // __ELOG_SOURCE_H__