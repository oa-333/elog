#ifndef __ELOG_FLUSH_POLICY_H__
#define __ELOG_FLUSH_POLICY_H__

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

#include "elog_common.h"
#include "elog_def.h"
#include "elog_target.h"

namespace elog {

/** @brief Initialize all flush policies (for internal use only). */
extern ELOG_API bool initFlushPolicies();

/** @brief Destroys all flush policies (for internal use only). */
extern ELOG_API void termFlushPolicies();

/**
 * @brief Flush policy. As some log targets are buffered, a flush policy should be defined to govern
 * the occasions on which the log target should be flushed so that log messages reach their
 * designated destination.
 */
class ELOG_API ELogFlushPolicy {
public:
    virtual ~ELogFlushPolicy() {}

    /** @brief Loads flush policy from property map. */
    virtual bool load(const std::string& logTargetCfg, const ELogPropertyMap& props) {
        return true;
    }

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

protected:
    ELogFlushPolicy(bool isActive = false) : m_isActive(isActive), m_logTarget(nullptr) {}
    ELogFlushPolicy(const ELogFlushPolicy&) = delete;
    ELogFlushPolicy(ELogFlushPolicy&&) = delete;

    // allow derived classes to modify active state
    inline void setActive() { m_isActive = true; }

    inline ELogTarget* getLogTarget() { return m_logTarget; }

    // helper for combined flush policy
    virtual void propagateLogTarget(ELogTarget* logTarget) {}

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
    /**
     * @brief Constructs a flush policy.
     * @return ELogFlushPolicy* The resulting flush policy, or null if failed.
     */
    virtual ELogFlushPolicy* constructFlushPolicy() = 0;

protected:
    /** @brief Constructor. */
    ELogFlushPolicyConstructor(const char* name) { registerFlushPolicyConstructor(name, this); }
};

/** @def Utility macro for declaring flush policy factory method registration. */
#define ELOG_DECLARE_FLUSH_POLICY(FlushPolicyType, Name)                            \
    class FlushPolicyType##Constructor : public elog::ELogFlushPolicyConstructor {  \
    public:                                                                         \
        FlushPolicyType##Constructor() : elog::ELogFlushPolicyConstructor(#Name) {} \
        elog::ELogFlushPolicy* constructFlushPolicy() final {                       \
            return new (std::nothrow) FlushPolicyType();                            \
        }                                                                           \
    };                                                                              \
    static FlushPolicyType##Constructor sConstructor;

/** @def Utility macro for implementing flush policy factory method registration. */
#define ELOG_IMPLEMENT_FLUSH_POLICY(FlushPolicyType) \
    FlushPolicyType::FlushPolicyType##Constructor FlushPolicyType::sConstructor;

/** @class A combined flush policy, for enforcing several flush policies. */
class ELOG_API ELogCombinedFlushPolicy : public ELogFlushPolicy {
public:
    ELogCombinedFlushPolicy() {}
    ELogCombinedFlushPolicy(const ELogCombinedFlushPolicy&) = delete;
    ELogCombinedFlushPolicy(ELogCombinedFlushPolicy&&) = delete;
    ~ELogCombinedFlushPolicy() {}

    inline void addFlushPolicy(ELogFlushPolicy* flushPolicy) {
        m_flushPolicies.push_back(flushPolicy);
        if (flushPolicy->isActive()) {
            setActive();
        }
    }

protected:
    std::vector<ELogFlushPolicy*> m_flushPolicies;

    // helper for combined flush policy
    virtual void propagateLogTarget(ELogTarget* logTarget) {
        for (uint32_t i = 0; i < m_flushPolicies.size(); ++i) {
            // this will get propagated to all active sub-policies
            m_flushPolicies[i]->setLogTarget(getLogTarget());
        }
    }
};

/** @class A combined flush policy, for enforcing all specified flush policies. */
class ELOG_API ELogAndFlushPolicy : public ELogCombinedFlushPolicy {
public:
    ELogAndFlushPolicy() {}
    ELogAndFlushPolicy(const ELogAndFlushPolicy&) = delete;
    ELogAndFlushPolicy(ELogAndFlushPolicy&&) = delete;
    ~ELogAndFlushPolicy() {}

    bool shouldFlush(uint32_t msgSizeBytes) final;
};

/** @class A combined flush policy, for enforcing one of many flush policies. */
class ELOG_API ELogOrFlushPolicy : public ELogCombinedFlushPolicy {
public:
    ELogOrFlushPolicy() {}
    ELogOrFlushPolicy(const ELogOrFlushPolicy&) = delete;
    ELogOrFlushPolicy(ELogOrFlushPolicy&&) = delete;
    ~ELogOrFlushPolicy() {}

    bool shouldFlush(uint32_t msgSizeBytes) final;
};

/** @class A immediate flush policy, for enforcing log target flush after every log message.  */
class ELOG_API ELogImmediateFlushPolicy : public ELogFlushPolicy {
public:
    ELogImmediateFlushPolicy() {}
    ELogImmediateFlushPolicy(const ELogPropertyMap& props) {}
    ELogImmediateFlushPolicy(const ELogImmediateFlushPolicy&) = delete;
    ELogImmediateFlushPolicy(ELogImmediateFlushPolicy&&) = delete;
    ~ELogImmediateFlushPolicy() {}

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
    ELogNeverFlushPolicy(const ELogPropertyMap& props) {}
    ELogNeverFlushPolicy(const ELogNeverFlushPolicy&) = delete;
    ELogNeverFlushPolicy(ELogNeverFlushPolicy&&) = delete;
    ~ELogNeverFlushPolicy() {}

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
    ELogCountFlushPolicy() : m_logCountLimit(0), m_currentLogCount(0) {}
    ELogCountFlushPolicy(uint64_t logCountLimit)
        : m_logCountLimit(logCountLimit), m_currentLogCount(0) {}
    ELogCountFlushPolicy(const ELogCountFlushPolicy&) = delete;
    ELogCountFlushPolicy(ELogCountFlushPolicy&&) = delete;
    ~ELogCountFlushPolicy() {}

    /** @brief Loads flush policy from property map. */
    bool load(const std::string& logTargetCfg, const ELogPropertyMap& props) final;

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
    ELogSizeFlushPolicy() : m_logSizeLimitBytes(0), m_currentLogSizeBytes(0) {}
    ELogSizeFlushPolicy(uint64_t logSizeLimitBytes)
        : m_logSizeLimitBytes(logSizeLimitBytes), m_currentLogSizeBytes(0) {}
    ELogSizeFlushPolicy(const ELogSizeFlushPolicy&) = delete;
    ELogSizeFlushPolicy(ELogSizeFlushPolicy&&) = delete;
    ~ELogSizeFlushPolicy() {}

    /** @brief Loads flush policy from property map. */
    bool load(const std::string& logTargetCfg, const ELogPropertyMap& props) final;

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
    ~ELogTimedFlushPolicy();

    /** @brief Loads flush policy from property map. */
    bool load(const std::string& logTargetCfg, const ELogPropertyMap& props) final;

    /** @brief Orders an active flush policy to start (by default no action takes place). */
    bool start() final;

    /** @brief Orders an active flush policy to stop (by default no action takes place). */
    bool stop() final;

    bool shouldFlush(uint32_t msgSizeBytes) final;

private:
    typedef std::chrono::time_point<std::chrono::steady_clock> Timestamp;
    typedef std::chrono::milliseconds Millis;

    inline Timestamp getTimestamp() const { return std::chrono::steady_clock::now(); }

    inline std::chrono::milliseconds getTimeDiff(Timestamp later, Timestamp earlier) const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(later - earlier);
    }

    Millis m_logTimeLimitMillis;
    std::atomic<Timestamp> m_prevFlushTime;
    std::mutex m_lock;
    std::condition_variable m_cv;
    bool m_stopTimer;
    std::thread m_timerThread;

    void onTimer();

    bool shouldStop();

    ELOG_DECLARE_FLUSH_POLICY(ELogTimedFlushPolicy, time);
};

}  // namespace elog

#endif  // __ELOG_FLUSH_POLICY_H__