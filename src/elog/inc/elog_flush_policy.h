#ifndef __ELOG_FLUSH_POLICY_H__
#define __ELOG_FLUSH_POLICY_H__

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

#include "elog_common_def.h"
#include "elog_config.h"
#include "elog_expression.h"
#include "elog_gc.h"
#include "elog_target_spec.h"
#include "elog_time.h"

namespace elog {

/** @def Default group flush size. */
#define ELOG_DEFAULT_GROUP_FLUSH_SIZE 16

/** @def Default group flush timeout (microseconds). */
#define ELOG_DEFAULT_GROUP_FLUSH_TIME_MICROS 200

// forward declaration
class ELOG_API ELogTarget;

/**
 * @brief Flush policy. As some log targets are buffered, a flush policy should be defined to govern
 * the occasions on which the log target should be flushed so that log messages reach their
 * designated destination.
 */
class ELOG_API ELogFlushPolicy {
public:
    virtual ~ELogFlushPolicy() {}

    /** @brief Loads flush policy from configuration. */
    virtual bool load(const ELogConfigMapNode* flushPolicyCfg) { return true; }

    /** @brief Loads flush policy from a free-style predicate-like parsed expression. */
    virtual bool loadExpr(const ELogExpression* expr) { return true; };

    /**
     * @brief Queries whether this flush policy is active (i.e. has a background thread that
     * actively operates on the containing log target).
     */
    inline bool isActive() const { return m_isActive; }

    /** @brief Installs the log target for an active flush policy. */
    inline void setLogTarget(ELogTarget* logTarget) {
        m_logTarget = logTarget;
        if (m_isActive) {
            propagateLogTarget(logTarget);
        }
    }

    /** @brief Orders an active flush policy to start (by default no action takes place). */
    virtual bool start() { return true; }

    /** @brief Orders an active flush policy to stop (by default no action takes place). */
    virtual bool stop() { return true; }

    /**
     * @brief Queries whether the log target should be flushed.
     * @param msgSizeBytes The current logged message size.
     * @return true If the log target should be flushed.
     */
    virtual bool shouldFlush(uint32_t msgSizeBytes) = 0;

    /**
     * @brief Allow flush policy also to moderate flush (i.e. hold back for a while, as in group
     * flush). By default no moderation takes place.
     * @return The operation result.
     */
    virtual bool moderateFlush(ELogTarget* logTarget);

protected:
    ELogFlushPolicy(bool isActive = false) : m_isActive(isActive), m_logTarget(nullptr) {}
    ELogFlushPolicy(const ELogFlushPolicy&) = delete;
    ELogFlushPolicy(ELogFlushPolicy&&) = delete;
    ELogFlushPolicy& operator=(const ELogFlushPolicy&) = delete;

    // allow derived classes to modify active state
    inline void setActive() { m_isActive = true; }

    inline ELogTarget* getLogTarget() { return m_logTarget; }

    // helper for combined flush policy
    virtual void propagateLogTarget(ELogTarget* logTarget) { (void)logTarget; }

    bool loadIntFlushPolicy(const ELogConfigMapNode* flushPolicyCfg, const char* flushPolicyName,
                            const char* propName, uint64_t& value);
    bool loadTimeoutFlushPolicy(const ELogConfigMapNode* flushPolicyCfg,
                                const char* flushPolicyName, const char* propName, uint64_t& value,
                                ELogTimeUnits targetUnits);
    bool loadSizeFlushPolicy(const ELogConfigMapNode* flushPolicyCfg, const char* flushPolicyName,
                             const char* propName, uint64_t& value, ELogSizeUnits targetUnits);

    bool loadIntFlushPolicy(const ELogExpression* expr, const char* flushPolicyName,
                            uint64_t& value, const char* propName = nullptr);
    bool loadTimeoutFlushPolicy(const ELogExpression* expr, const char* flushPolicyName,
                                uint64_t& value, ELogTimeUnits targetUnits,
                                const char* propName = nullptr);
    bool loadSizeFlushPolicy(const ELogExpression* expr, const char* flushPolicyName,
                             uint64_t& value, ELogSizeUnits targetUnits,
                             const char* propName = nullptr);

private:
    bool m_isActive;
    ELogTarget* m_logTarget;
};

// forward declaration
class ELOG_API ELogFlushPolicyConstructor;

/**
 * @brief Flush policy constructor registration helper.
 * @param name The flush policy identifier.
 * @param allocator The flush policy constructor.
 */
extern ELOG_API void registerFlushPolicyConstructor(const char* name,
                                                    ELogFlushPolicyConstructor* constructor);

/**
 * @brief Utility helper for constructing a flush policy from type name identifier.
 * @param name The flush policy identifier.
 * @return ELogFlushPolicy* The resulting flush policy, or null if failed.
 */
extern ELOG_API ELogFlushPolicy* constructFlushPolicy(const char* name);

/** @brief Utility helper class for flush policy construction. */
class ELOG_API ELogFlushPolicyConstructor {
public:
    virtual ~ELogFlushPolicyConstructor() {}

    /**
     * @brief Constructs a flush policy.
     * @return ELogFlushPolicy* The resulting flush policy, or null if failed.
     */
    virtual ELogFlushPolicy* constructFlushPolicy() = 0;

protected:
    /** @brief Constructor. */
    ELogFlushPolicyConstructor(const char* name) { registerFlushPolicyConstructor(name, this); }
    ELogFlushPolicyConstructor(const ELogFlushPolicyConstructor&) = delete;
    ELogFlushPolicyConstructor(ELogFlushPolicyConstructor&&) = delete;
    ELogFlushPolicyConstructor& operator=(const ELogFlushPolicyConstructor&) = delete;
};

/** @def Utility macro for declaring flush policy factory method registration. */
#define ELOG_DECLARE_FLUSH_POLICY(FlushPolicyType, Name)                                       \
    class ELOG_API FlushPolicyType##Constructor : public elog::ELogFlushPolicyConstructor {    \
    public:                                                                                    \
        FlushPolicyType##Constructor() : elog::ELogFlushPolicyConstructor(#Name) {}            \
        elog::ELogFlushPolicy* constructFlushPolicy() final {                                  \
            return new (std::nothrow) FlushPolicyType();                                       \
        }                                                                                      \
        ~FlushPolicyType##Constructor() final {}                                               \
        FlushPolicyType##Constructor(const FlushPolicyType##Constructor&) = delete;            \
        FlushPolicyType##Constructor(FlushPolicyType##Constructor&&) = delete;                 \
        FlushPolicyType##Constructor& operator=(const FlushPolicyType##Constructor&) = delete; \
    };                                                                                         \
    static FlushPolicyType##Constructor sConstructor;

/** @def Utility macro for implementing flush policy factory method registration. */
#define ELOG_IMPLEMENT_FLUSH_POLICY(FlushPolicyType) \
    FlushPolicyType::FlushPolicyType##Constructor FlushPolicyType::sConstructor;

/** @class A compound flush policy, for enforcing several flush policies. */
class ELOG_API ELogCompoundFlushPolicy : public ELogFlushPolicy {
public:
    ELogCompoundFlushPolicy() {}
    ELogCompoundFlushPolicy(const ELogCompoundFlushPolicy&) = delete;
    ELogCompoundFlushPolicy(ELogCompoundFlushPolicy&&) = delete;
    ELogCompoundFlushPolicy& operator=(const ELogCompoundFlushPolicy&) = delete;
    ~ELogCompoundFlushPolicy() override {}

    /** @brief Loads flush policy from configuration. */
    bool load(const ELogConfigMapNode* flushPolicyCfg) override;

    inline void addFlushPolicy(ELogFlushPolicy* flushPolicy) {
        m_flushPolicies.push_back(flushPolicy);
        if (flushPolicy->isActive()) {
            setActive();
        }
    }

protected:
    std::vector<ELogFlushPolicy*> m_flushPolicies;

    // helper for combined flush policy
    void propagateLogTarget(ELogTarget* logTarget) override;

    /** @brief Loads flush policy from a free-style predicate-like parsed expression. */
    bool loadCompositeExpr(const ELogCompositeExpression* expr);
};

/** @class A combined flush policy, for enforcing all specified flush policies. */
class ELOG_API ELogAndFlushPolicy : public ELogCompoundFlushPolicy {
public:
    ELogAndFlushPolicy() {}
    ELogAndFlushPolicy(const ELogAndFlushPolicy&) = delete;
    ELogAndFlushPolicy(ELogAndFlushPolicy&&) = delete;
    ELogAndFlushPolicy& operator=(const ELogAndFlushPolicy&) = delete;
    ~ELogAndFlushPolicy() override {}

    /** @brief Loads flush policy from a free-style predicate-like parsed expression. */
    bool loadExpr(const ELogExpression* expr) override;

    bool shouldFlush(uint32_t msgSizeBytes) final;

private:
    ELOG_DECLARE_FLUSH_POLICY(ELogAndFlushPolicy, AND);
};

/** @class A combined flush policy, for enforcing one of many flush policies. */
class ELOG_API ELogOrFlushPolicy : public ELogCompoundFlushPolicy {
public:
    ELogOrFlushPolicy() {}
    ELogOrFlushPolicy(const ELogOrFlushPolicy&) = delete;
    ELogOrFlushPolicy(ELogOrFlushPolicy&&) = delete;
    ELogOrFlushPolicy& operator=(const ELogOrFlushPolicy&) = delete;
    ~ELogOrFlushPolicy() override {}

    /** @brief Loads flush policy from a free-style predicate-like parsed expression. */
    bool loadExpr(const ELogExpression* expr) override;

    bool shouldFlush(uint32_t msgSizeBytes) final;

private:
    ELOG_DECLARE_FLUSH_POLICY(ELogOrFlushPolicy, OR);
};

/** @brief A log flush policy that negates the result of another log flush policy. */
class ELOG_API ELogNotFlushPolicy : public ELogFlushPolicy {
public:
    ELogNotFlushPolicy(ELogFlushPolicy* flushPolicy = nullptr) : m_flushPolicy(flushPolicy) {}
    ELogNotFlushPolicy(const ELogNotFlushPolicy&) = delete;
    ELogNotFlushPolicy(ELogNotFlushPolicy&&) = delete;
    ELogNotFlushPolicy& operator=(const ELogNotFlushPolicy&) = delete;
    ~ELogNotFlushPolicy() override {}

    /** @brief Loads flush policy from configuration. */
    bool load(const ELogConfigMapNode* flushPolicyCfg) final;

    /** @brief Loads flush policy from a free-style predicate-like parsed expression. */
    bool loadExpr(const ELogExpression* expr) override;

    bool shouldFlush(uint32_t msgSizeBytes) final {
        return !m_flushPolicy->shouldFlush(msgSizeBytes);
    }

private:
    ELogFlushPolicy* m_flushPolicy;

    ELOG_DECLARE_FLUSH_POLICY(ELogNotFlushPolicy, NOT);
};

/** @class A immediate flush policy, for enforcing log target flush after every log message.  */
class ELOG_API ELogImmediateFlushPolicy : public ELogFlushPolicy {
public:
    ELogImmediateFlushPolicy() {}
    ELogImmediateFlushPolicy(const ELogImmediateFlushPolicy&) = delete;
    ELogImmediateFlushPolicy(ELogImmediateFlushPolicy&&) = delete;
    ELogImmediateFlushPolicy& operator=(const ELogImmediateFlushPolicy&) = delete;
    ~ELogImmediateFlushPolicy() override {}

    bool shouldFlush(uint32_t msgSizeBytes) final;

private:
    ELOG_DECLARE_FLUSH_POLICY(ELogImmediateFlushPolicy, immediate);
};

/**
 * @class A never flush policy, for ensuring log target is never flushed, except for during
 * shutdown.
 */
class ELOG_API ELogNeverFlushPolicy : public ELogFlushPolicy {
public:
    ELogNeverFlushPolicy() {}
    ELogNeverFlushPolicy(const ELogNeverFlushPolicy&) = delete;
    ELogNeverFlushPolicy(ELogNeverFlushPolicy&&) = delete;
    ELogNeverFlushPolicy& operator=(const ELogNeverFlushPolicy&) = delete;
    ~ELogNeverFlushPolicy() override {}

    bool shouldFlush(uint32_t msgSizeBytes) final;

private:
    ELOG_DECLARE_FLUSH_POLICY(ELogNeverFlushPolicy, never);
};

/**
 * @class A flush policy that enforces log target flush whenever the number of un-flushed log
 * messages exceeds a configured limit.
 */
class ELOG_API ELogCountFlushPolicy : public ELogFlushPolicy {
public:
    ELogCountFlushPolicy(uint64_t logCountLimit = 0)
        : m_logCountLimit(logCountLimit), m_currentLogCount(0) {}
    ELogCountFlushPolicy(const ELogCountFlushPolicy&) = delete;
    ELogCountFlushPolicy(ELogCountFlushPolicy&&) = delete;
    ELogCountFlushPolicy& operator=(const ELogCountFlushPolicy&) = delete;
    ~ELogCountFlushPolicy() override {}

    /** @brief Loads flush policy from configuration. */
    bool load(const ELogConfigMapNode* flushPolicyCfg) final;

    /** @brief Loads flush policy from a free-style predicate-like parsed expression. */
    bool loadExpr(const ELogExpression* expr) final;

    bool shouldFlush(uint32_t msgSizeBytes) final;

private:
    uint64_t m_logCountLimit;
    std::atomic<uint64_t> m_currentLogCount;

    ELOG_DECLARE_FLUSH_POLICY(ELogCountFlushPolicy, count);
};

/**
 * @class A flush policy the enforces log target flush whenever the total size of un-flushed log
 * messages exceeds a configured limit.
 */
class ELOG_API ELogSizeFlushPolicy : public ELogFlushPolicy {
public:
    ELogSizeFlushPolicy(uint64_t logSizeLimitBytes = 0)
        : m_logSizeLimitBytes(logSizeLimitBytes), m_currentLogSizeBytes(0) {}
    ELogSizeFlushPolicy(const ELogSizeFlushPolicy&) = delete;
    ELogSizeFlushPolicy(ELogSizeFlushPolicy&&) = delete;
    ELogSizeFlushPolicy& operator=(const ELogSizeFlushPolicy&) = delete;
    ~ELogSizeFlushPolicy() override {}

    /** @brief Loads flush policy from configuration. */
    bool load(const ELogConfigMapNode* flushPolicyCfg) final;

    /** @brief Loads flush policy from a free-style predicate-like parsed expression. */
    bool loadExpr(const ELogExpression* expr) final;

    bool shouldFlush(uint32_t msgSizeBytes) final;

private:
    uint64_t m_logSizeLimitBytes;
    std::atomic<uint64_t> m_currentLogSizeBytes;

    ELOG_DECLARE_FLUSH_POLICY(ELogSizeFlushPolicy, size);
};

/**
 * @class A flush policy the enforces log target flush whenever the time that passed since recent
 * log message flush exceeds a configured time limit. This is an active policy, and it should be
 * combined with a log target, such that when flush time arrives, the log target will get flushed.
 */
class ELOG_API ELogTimedFlushPolicy : public ELogFlushPolicy {
public:
    ELogTimedFlushPolicy();
    ELogTimedFlushPolicy(uint64_t logTimeLimitMillis, ELogTarget* logTarget);
    ELogTimedFlushPolicy(const ELogTimedFlushPolicy&) = delete;
    ELogTimedFlushPolicy(ELogTimedFlushPolicy&&) = delete;
    ELogTimedFlushPolicy& operator=(const ELogTimedFlushPolicy&) = delete;
    ~ELogTimedFlushPolicy();

    /** @brief Loads flush policy from configuration. */
    bool load(const ELogConfigMapNode* flushPolicyCfg) final;

    /** @brief Loads flush policy from a free-style predicate-like parsed expression. */
    bool loadExpr(const ELogExpression* expr) final;

    /** @brief Orders an active flush policy to start (by default no action takes place). */
    bool start() final;

    /** @brief Orders an active flush policy to stop (by default no action takes place). */
    bool stop() final;

    bool shouldFlush(uint32_t msgSizeBytes) final;

private:
    inline ELogTime getTimestamp() const {
        ELogTime currentTime;
        elogGetCurrentTime(currentTime);
        return currentTime;
    }

    inline uint64_t getTimeDiffMillis(const ELogTime& later, const ELogTime& earlier) const {
        return (elogTimeToUTCNanos(later) - elogTimeToUTCNanos(earlier)) / 1000000ull;
    }

    uint64_t m_logTimeLimitMillis;
    std::atomic<ELogTime> m_prevFlushTime;
    std::mutex m_lock;
    std::condition_variable m_cv;
    bool m_stopTimer;
    std::thread m_timerThread;

    void onTimer();

    bool shouldStop();

    ELOG_DECLARE_FLUSH_POLICY(ELogTimedFlushPolicy, time);
};

class ELOG_API ELogChainedFlushPolicy : public ELogFlushPolicy {
public:
    ELogChainedFlushPolicy(ELogFlushPolicy* controlPolicy = nullptr,
                           ELogFlushPolicy* moderatePolicy = nullptr)
        : m_controlPolicy(controlPolicy), m_moderatePolicy(moderatePolicy) {}
    ELogChainedFlushPolicy(const ELogChainedFlushPolicy&) = delete;
    ELogChainedFlushPolicy(ELogChainedFlushPolicy&&) = delete;
    ELogChainedFlushPolicy& operator=(const ELogChainedFlushPolicy&) = delete;
    ~ELogChainedFlushPolicy() final {
        setControlFlushPolicy(nullptr);
        setModerateFlushPolicy(nullptr);
    }

    /** @brief Loads flush policy from configuration. */
    bool load(const ELogConfigMapNode* flushPolicyCfg) final;

    /** @brief Loads flush policy from a free-style predicate-like parsed expression. */
    bool loadExpr(const ELogExpression* expr) final;

    /** @brief Orders an active flush policy to start (by default no action takes place). */
    bool start() override;

    /** @brief Orders an active flush policy to stop (by default no action takes place). */
    bool stop() override;

    /** @brief Sets the control flush policy (determines whether should flush). */
    inline void setControlFlushPolicy(ELogFlushPolicy* flushPolicy) {
        if (m_controlPolicy != nullptr) {
            delete m_controlPolicy;
        }
        m_controlPolicy = flushPolicy;
    }

    /** @brief Sets the moderate flush policy (determines how many threads flush together). */
    inline void setModerateFlushPolicy(ELogFlushPolicy* flushPolicy) {
        if (m_moderatePolicy != nullptr) {
            delete m_moderatePolicy;
        }
        m_moderatePolicy = flushPolicy;
    }

    /**
     * @brief Queries whether the log target should be flushed.
     * @param msgSizeBytes The current logged message size.
     * @return true If the log target should be flushed.
     */
    bool shouldFlush(uint32_t msgSizeBytes) final {
        return m_controlPolicy->shouldFlush(msgSizeBytes);
    }

    /**
     * @brief Allow flush policy also to moderate flush (i.e. hold back for a while, as in group
     * flush). By default no moderation takes place.
     */
    bool moderateFlush(ELogTarget* logTarget) final {
        return m_moderatePolicy->moderateFlush(logTarget);
    }

protected:
    // helper for combined flush policy
    void propagateLogTarget(ELogTarget* logTarget) override;

private:
    ELogFlushPolicy* m_controlPolicy;
    ELogFlushPolicy* m_moderatePolicy;

    ELogFlushPolicy* loadSubFlushPolicy(const char* typeName, const char* propName,
                                        const ELogConfigMapNode* flushPolicyCfg);

    ELOG_DECLARE_FLUSH_POLICY(ELogChainedFlushPolicy, CHAIN);
};

class ELOG_API ELogGroupFlushPolicy : public ELogFlushPolicy {
public:
    ELogGroupFlushPolicy(ELogFlushPolicy* flushPolicy = nullptr,
                         uint32_t groupSize = ELOG_DEFAULT_GROUP_FLUSH_SIZE,
                         uint32_t groupTimeoutMicros = ELOG_DEFAULT_GROUP_FLUSH_TIME_MICROS)
        : m_flushPolicy(flushPolicy),
          m_groupSize(groupSize),
          m_groupTimeoutMicros(groupTimeoutMicros),
          m_currentGroup(nullptr),
          m_epoch(0) {}
    ELogGroupFlushPolicy(const ELogGroupFlushPolicy&) = delete;
    ELogGroupFlushPolicy(ELogGroupFlushPolicy&&) = delete;
    ELogGroupFlushPolicy& operator=(const ELogGroupFlushPolicy&) = delete;
    ~ELogGroupFlushPolicy() final {}

    /** @brief Loads flush policy from configuration. */
    bool load(const ELogConfigMapNode* flushPolicyCfg) final;

    /** @brief Loads flush policy from a free-style predicate-like parsed expression. */
    bool loadExpr(const ELogExpression* expr) final;

    /** @brief Orders an active flush policy to start (by default no action takes place). */
    bool start() final;

    /** @brief Orders an active flush policy to stop (by default no action takes place). */
    bool stop() final;

    /**
     * @brief Queries whether the log target should be flushed.
     * @param msgSizeBytes The current logged message size.
     * @return true If the log target should be flushed.
     */
    bool shouldFlush(uint32_t msgSizeBytes) final;

    /**
     * @brief Allow flush policy also to moderate flush (i.e. hold back for a while, as in group
     * flush). By default no moderation takes place.
     */
    bool moderateFlush(ELogTarget* logTarget) final;

private:
    typedef std::chrono::microseconds Micros;

    ELogFlushPolicy* m_flushPolicy;
    uint64_t m_groupSize;
    Micros m_groupTimeoutMicros;

    class Group : public ELogManagedObject {
    public:
        Group(ELogTarget* logTarget, uint64_t groupSize, Micros groupTimeoutMicros);
        Group(const Group&) = delete;
        Group(Group&&) = delete;
        Group& operator=(const Group&) = delete;
        ~Group() override {}

        bool join();
        bool execLeader();
        void execFollower();

    private:
        ELogTarget* m_logTarget;
        uint64_t m_groupSize;
        Micros m_groupTimeoutMicros;
        uint64_t m_memberCount;
        enum class State : uint32_t { WAIT, FULL, CLOSED, FLUSH_DONE, ALL_LEFT } m_state;
        std::mutex m_lock;
        std::condition_variable m_cv;
        uint32_t m_leaderThreadId;
    };

    ELogGC m_gc;

    std::atomic<Group*> m_currentGroup;
    std::atomic<uint64_t> m_epoch;

    ELOG_DECLARE_FLUSH_POLICY(ELogGroupFlushPolicy, group);
};

}  // namespace elog

#endif  // __ELOG_FLUSH_POLICY_H__