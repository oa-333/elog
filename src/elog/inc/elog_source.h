#ifndef __ELOG_SOURCE_H__
#define __ELOG_SOURCE_H__

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "elog_common_def.h"
#include "elog_level.h"

namespace elog {

// forward declaration
class ELogLogger;

/**
 * @brief A log source represents a logical module with a designated log level, and managed loggers.
 * Each object needing a logger will contact the log source associated with object's module (as the
 * application semantically defines), and ask for a logger by calling @ref createLogger(). The
 * loggers life cycle is managed by the log source. Log sources are hierarchical, and the elog
 * system pre-defines a root log source, from which a default logger stems. When setting the log
 * level of a log source, all managed loggers are affected immediately.
 */
class ELOG_API ELogSource {
public:
    ELogSource(const ELogSource&) = delete;
    ELogSource(ELogSource&&) = delete;

    // log source name/id
    /** @brief Retrieves the unique log source id. */
    inline ELogSourceId getId() const { return m_sourceId; }

    /** @brief Retrieves the name of the log source. */
    inline const char* getName() const { return m_name.c_str(); }

    /** @brief Retrieves the qualified name (from root) of the log source. */
    inline const char* getQualifiedName() const { return m_qname.c_str(); }

    /** @brief Retrieves the qualified name length of the log source. */
    inline size_t getQualifiedNameLength() const { return m_qname.length(); }

    /**
     * @brief Sets a semantic module name that is associated with the log source (used for logging,
     * and is accessible by the ${module} log line format specifier).
     */
    inline void setModuleName(const char* moduleName) { m_moduleName = moduleName; }

    /** @brief Retrieves the module name associated with the log source. */
    inline const char* getModuleName() const { return m_moduleName.c_str(); }

    /** @brief Retrieves the length of the module name associated with the log source. */
    inline size_t getModuleNameLength() const { return m_moduleName.length(); }

    /**
     * @brief Retrieves the parent log source of this log source. The root log source has no
     * parent.
     */
    inline const ELogSource* getParent() const { return m_parent; }

    /**
     * @brief Adds a child log source to this log source.
     * @param logSource The log source to add.
     * @return true If the child log source was added successfully.
     * @return false A child log source with the same name already exists (and therefore the request
     * to add the child was rejected).
     */
    bool addChild(ELogSource* logSource);

    /** @brief Retrieves a child log source by name. Returns null if not found. */
    ELogSource* getChild(const char* name);

    /** @brief Queries for existence of a child by name. */
    inline bool containsChild(const char* name) { return getChild(name) != nullptr; }

    /** @brief Removes a child log source by name. Silently ignored if child not found. */
    void removeChild(const char* name);

    /**
     * @brief Sets the log level associated with the log source and all of its managed loggers.
     * @param logLevel The log level to set.
     * @param propagateMode Specifies how the log level should be propagated to child log sources,
     * if at all.
     */
    void setLogLevel(ELogLevel logLevel, ELogPropagateMode propagateMode);

    /** @brief Queries whether the log source can log a record with the given log level. */
    inline bool canLog(ELogLevel logLevel) {
        return static_cast<int>(logLevel) <= static_cast<int>(m_logLevel);
    }

    /** @brief Sets log target affinity. */
    inline void setLogTargetAffinity(ELogTargetAffinityMask logTargetAffinityMask) {
        m_logTargetAffinityMask = logTargetAffinityMask;
    }

    /** @brief Adds a log target to the log target affinity mask of the log source. */
    inline bool addLogTargetAffinity(ELogTargetId logTargetId) {
        if (logTargetId > ELOG_MAX_LOG_TARGET_ID_AFFINITY) {
            return false;
        }
        ELOG_ADD_TARGET_AFFINITY_MASK(m_logTargetAffinityMask, logTargetId);
        return true;
    }

    /** @brief Removes a log target from the log target affinity mask of the log source. */
    inline bool removeLogTargetAffinity(ELogTargetId logTargetId) {
        if (logTargetId > ELOG_MAX_LOG_TARGET_ID_AFFINITY) {
            return false;
        }
        ELOG_REMOVE_TARGET_AFFINITY_MASK(m_logTargetAffinityMask, logTargetId);
        return true;
    }

    /** @brief Retrieves the log target affinity mask configured for this log source. */
    inline ELogTargetId getLogTargetAffinityMask() const { return m_logTargetAffinityMask; }

    /**
     * @brief Obtains a logger that may be invoked by more than one thread. The logger is managed
     * by the log source and should not be deleted by the caller.
     */
    ELogLogger* createSharedLogger();

    /**
     * @brief Obtains a logger that can be invoked by only one thread. The logger is managed
     * by the log source and should not be deleted by the caller.
     */
    ELogLogger* createPrivateLogger();

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
    ELogTargetAffinityMask m_logTargetAffinityMask;

    ELogSource(ELogSourceId sourceId, const char* name, ELogSource* parent = nullptr,
               ELogLevel logLevel = ELEVEL_INFO);
    ~ELogSource();

    void propagateLogLevel(ELogLevel logLevel, ELogPropagateMode propagateMode);

    friend class ELogSystem;
};

}  // namespace elog

#endif  // __ELOG_SOURCE_H__