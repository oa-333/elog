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

    /** @enum Log level propagation mode constants. */
    enum class PropagateMode : uint32_t {
        /** @brief Designates that log level should not be propagated to child log sources. */
        PM_NONE,

        /** @brief Designates that log level should be propagated to child log sources as is. */
        PM_SET,

        /**
         * @brief Designates that log level should be propagated to child log sources such that
         * child log sources are to be restricted not to have looser log level than that of their
         * parent.
         * @note Strict log level have lower log level value.
         */
        PM_RESTRICT,

        /**
         * @brief Designates that log level should be propagated to child log sources such that the
         * log level of child log sources should be loosened, if necessary, to ensure that it is at
         * least as loose as the log level of the parent.
         * @note Strict log level have lower log level value.
         */
        PM_LOOSE
    };

    // log source name/id
    /** @brief Retrieves the unique log source id. */
    inline ELogSourceId getId() const { return m_sourceId; }

    /** @brief Retrieves the name of the log source. */
    inline const char* getName() const { return m_name.c_str(); }

    /** @brief Retrieves the qualified name (from root) of the log source. */
    inline const char* getQualifiedName() const { return m_qname.c_str(); }

    /**
     * @brief Sets a semantic module name that is associated with the log source (used for logging,
     * and is accessible by the ${module} log line format specifier).
     */
    inline void setModuleName(const char* moduleName) { m_moduleName = moduleName; }

    /** @brief Retrieves the module name associated with the log source. */
    inline const char* getModuleName() const { return m_moduleName.c_str(); }

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
    void setLogLevel(ELogLevel logLevel, PropagateMode propagateMode);

    /** @brief Queries whether the log source can log a record with the given log level. */
    inline bool canLog(ELogLevel logLevel) {
        return static_cast<int>(logLevel) <= static_cast<int>(m_logLevel);
    }

    /** @brief Restricts this log source to a specific log target. */
    inline void restrictToLogTarget(ELogTargetId logTargetId) { m_logTargetId = logTargetId; }

    /** @brief Retrieves the id of the log target to which this log source is restricted. */
    inline ELogTargetId getRestrictLogTargetId() const { return m_logTargetId; }

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
    ELogTargetId m_logTargetId;

    ELogSource(ELogSourceId sourceId, const char* name, ELogSource* parent = nullptr,
               ELogLevel logLevel = ELEVEL_INFO);
    ~ELogSource();

    void propagateLogLevel(ELogLevel logLevel, PropagateMode propagateMode);

    friend class ELogSystem;
};

}  // namespace elog

#endif  // __ELOG_SOURCE_H__