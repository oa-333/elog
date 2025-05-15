#include "elog_flush_policy.h"

#include <unordered_map>

#include "elog_common.h"
#include "elog_config_loader.h"
#include "elog_error.h"

namespace elog {

ELOG_IMPLEMENT_FLUSH_POLICY(ELogAndFlushPolicy);
ELOG_IMPLEMENT_FLUSH_POLICY(ELogOrFlushPolicy);
ELOG_IMPLEMENT_FLUSH_POLICY(ELogNeverFlushPolicy)
ELOG_IMPLEMENT_FLUSH_POLICY(ELogImmediateFlushPolicy)
ELOG_IMPLEMENT_FLUSH_POLICY(ELogCountFlushPolicy)
ELOG_IMPLEMENT_FLUSH_POLICY(ELogSizeFlushPolicy)
ELOG_IMPLEMENT_FLUSH_POLICY(ELogTimedFlushPolicy)

#define ELOG_MAX_FLUSH_POLICY_COUNT 100

struct ELogFlushPolicyNameConstructor {
    const char* m_name;
    ELogFlushPolicyConstructor* m_ctor;
};

static ELogFlushPolicyNameConstructor sFlushPolicyConstructors[ELOG_MAX_FLUSH_POLICY_COUNT] = {};
static uint32_t sFlushPolicyConstructorsCount = 0;

typedef std::unordered_map<std::string, ELogFlushPolicyConstructor*> ELogFlushPolicyConstructorMap;

static ELogFlushPolicyConstructorMap sFlushPolicyConstructorMap;

void registerFlushPolicyConstructor(const char* name, ELogFlushPolicyConstructor* constructor) {
    // due to c runtime issues we delay access to unordered map
    if (sFlushPolicyConstructorsCount >= ELOG_MAX_FLUSH_POLICY_COUNT) {
        ELOG_REPORT_ERROR("Cannot register flush policy constructor, no space: %s", name);
        exit(1);
    } else {
        sFlushPolicyConstructors[sFlushPolicyConstructorsCount++] = {name, constructor};
    }
}

static bool applyFlushPolicyConstructorRegistration() {
    for (uint32_t i = 0; i < sFlushPolicyConstructorsCount; ++i) {
        ELogFlushPolicyNameConstructor& nameCtorPair = sFlushPolicyConstructors[i];
        if (!sFlushPolicyConstructorMap
                 .insert(ELogFlushPolicyConstructorMap::value_type(nameCtorPair.m_name,
                                                                   nameCtorPair.m_ctor))
                 .second) {
            ELOG_REPORT_ERROR("Duplicate flush policy identifier: %s", nameCtorPair.m_name);
            return false;
        }
    }
    return true;
}

bool initFlushPolicies() { return applyFlushPolicyConstructorRegistration(); }

void termFlushPolicies() { sFlushPolicyConstructorMap.clear(); }

ELogFlushPolicy* constructFlushPolicy(const char* name) {
    ELogFlushPolicyConstructorMap::iterator itr = sFlushPolicyConstructorMap.find(name);
    if (itr == sFlushPolicyConstructorMap.end()) {
        ELOG_REPORT_ERROR("Invalid flush policy %s: not found", name);
        return nullptr;
    }

    ELogFlushPolicyConstructor* constructor = itr->second;
    ELogFlushPolicy* flushPolicy = constructor->constructFlushPolicy();
    if (flushPolicy == nullptr) {
        ELOG_REPORT_ERROR("Failed to create flush policy, out of memory");
    }
    return flushPolicy;
}

bool ELogCompoundFlushPolicy::load(const std::string& logTargetCfg,
                                   const ELogTargetNestedSpec& logTargetSpec) {
    // we expect to find a nested property 'flush_policy_args' with one or more array item
    ELogTargetNestedSpec::SubSpecMap::const_iterator itr =
        logTargetSpec.m_subSpec.find("flush_policy_args");
    if (itr == logTargetSpec.m_subSpec.end()) {
        ELOG_REPORT_ERROR(
            "Missing 'flush_policy_args' nested property required for compound flush policy: %s",
            logTargetCfg.c_str());
        return false;
    }

    const ELogTargetNestedSpec::SubSpecList& subSpecList = itr->second;
    if (subSpecList.empty()) {
        ELOG_REPORT_ERROR(
            "Nested property 'flush_policy_args' (required for compound flush policy) is empty: %s",
            logTargetCfg.c_str());
        return false;
    }
    for (uint32_t i = 0; i < subSpecList.size(); ++i) {
        const ELogTargetNestedSpec& subSpec = subSpecList[i];
        bool result = true;
        ELogFlushPolicy* flushPolicy =
            ELogConfigLoader::loadFlushPolicy(logTargetCfg, subSpec, false, result);
        if (!result) {
            ELOG_REPORT_ERROR(
                "Failed to load %uth sub-flush-policy for compound flush policy: %s (see previous "
                "errors)",
                i, logTargetCfg.c_str());
            return false;
        }
        if (flushPolicy == nullptr) {
            ELOG_REPORT_ERROR(
                "Failed to load %uth sub-flush-policy for compound flush policy, policy "
                "specification not found: %s",
                i, logTargetCfg.c_str());
            return false;
        }
        addFlushPolicy(flushPolicy);
    }
    return true;
}

bool ELogAndFlushPolicy::shouldFlush(uint32_t msgSizeBytes) {
    // even through we can stop early, each flush policy may need to accumulate log message size in
    // order to make decisions
    bool res = true;
    for (ELogFlushPolicy* flushPolicy : m_flushPolicies) {
        if (!flushPolicy->shouldFlush(msgSizeBytes)) {
            res = false;
        }
    }
    return res;
}

bool ELogOrFlushPolicy::shouldFlush(uint32_t msgSizeBytes) {
    // even through we can stop early, each flush policy may need to accumulate log message size in
    // order to make decisions
    bool res = false;
    for (ELogFlushPolicy* flushPolicy : m_flushPolicies) {
        if (flushPolicy->shouldFlush(msgSizeBytes)) {
            res = true;
        }
    }
    return false;
}

bool ELogImmediateFlushPolicy::shouldFlush(uint32_t msgSizeBytes) { return true; }

bool ELogNeverFlushPolicy::shouldFlush(uint32_t msgSizeBytes) { return false; }

bool ELogCountFlushPolicy::load(const std::string& logTargetCfg,
                                const ELogTargetNestedSpec& logTargetSpec) {
    ELogPropertyMap::const_iterator itr = logTargetSpec.m_spec.m_props.find("flush_count");
    if (itr == logTargetSpec.m_spec.m_props.end()) {
        ELOG_REPORT_ERROR(
            "Invalid flush policy configuration, missing expected flush_count property for "
            "flush_policy=count: %s",
            logTargetCfg.c_str());
        return false;
    }
    if (!parseIntProp("flush_count", logTargetCfg, itr->second, m_logCountLimit, true)) {
        ELOG_REPORT_ERROR(
            "Invalid flush policy configuration, flush_count property value '%s' is an "
            "ill-formed integer: %s",
            itr->second.c_str(), logTargetCfg.c_str());
        return false;
    }
    return true;
}

bool ELogCountFlushPolicy::shouldFlush(uint32_t msgSizeBytes) {
    uint64_t logCount = m_currentLogCount.fetch_add(1, std::memory_order_relaxed);
    return (logCount % m_logCountLimit == 0);
}

bool ELogSizeFlushPolicy::load(const std::string& logTargetCfg,
                               const ELogTargetNestedSpec& logTargetSpec) {
    ELogPropertyMap::const_iterator itr = logTargetSpec.m_spec.m_props.find("flush_size_bytes");
    if (itr == logTargetSpec.m_spec.m_props.end()) {
        ELOG_REPORT_ERROR(
            "Invalid flush policy configuration, missing expected flush_size_bytes "
            "property for flush_policy=size: %s",
            logTargetCfg.c_str());
        return false;
    }
    if (!parseIntProp("flush_size_bytes", logTargetCfg, itr->second, m_logSizeLimitBytes, true)) {
        ELOG_REPORT_ERROR(
            "Invalid flush policy configuration, flush_size_bytes property value '%s' is "
            "an ill-formed integer: %s",
            itr->second.c_str(), logTargetCfg.c_str());
        return false;
    }
    return true;
}

bool ELogSizeFlushPolicy::shouldFlush(uint32_t msgSizeBytes) {
    uint64_t prevSizeBytes =
        m_currentLogSizeBytes.fetch_add(msgSizeBytes, std::memory_order_relaxed);
    uint64_t currSizeBytes = prevSizeBytes + msgSizeBytes;
    return (currSizeBytes / m_logSizeLimitBytes) > (prevSizeBytes / m_logSizeLimitBytes);
}

ELogTimedFlushPolicy::ELogTimedFlushPolicy()
    : ELogFlushPolicy(true),
      m_prevFlushTime(getTimestamp()),
      m_logTimeLimitMillis(0),
      m_stopTimer(false) {}

ELogTimedFlushPolicy::ELogTimedFlushPolicy(uint64_t logTimeLimitMillis, ELogTarget* logTarget)
    : ELogFlushPolicy(true),
      m_prevFlushTime(getTimestamp()),
      m_logTimeLimitMillis(logTimeLimitMillis),
      m_stopTimer(false) {}

ELogTimedFlushPolicy::~ELogTimedFlushPolicy() {}

bool ELogTimedFlushPolicy::load(const std::string& logTargetCfg,
                                const ELogTargetNestedSpec& logTargetSpec) {
    ELogPropertyMap::const_iterator itr = logTargetSpec.m_spec.m_props.find("flush_timeout_millis");
    if (itr == logTargetSpec.m_spec.m_props.end()) {
        ELOG_REPORT_ERROR(
            "Invalid flush policy configuration, missing expected flush_timeout_millis "
            "property for flush_policy=time: %s",
            logTargetCfg.c_str());
        return false;
    }
    uint64_t logTimeLimitMillis = 0;
    if (!parseIntProp("flush_timeout_millis", logTargetCfg, itr->second, logTimeLimitMillis,
                      true)) {
        ELOG_REPORT_ERROR(
            "Invalid flush policy configuration, flush_timeout_millis property value '%s' "
            "is an ill-formed integer: %s",
            itr->second.c_str(), logTargetCfg.c_str());
        return false;
    }
    m_logTimeLimitMillis = Millis(logTimeLimitMillis);
    return true;
}

bool ELogTimedFlushPolicy::start() {
    m_timerThread = std::thread(&ELogTimedFlushPolicy::onTimer, this);
    return true;
}

bool ELogTimedFlushPolicy::stop() {
    // raise stop flag and wakeup timer thread
    {
        std::unique_lock<std::mutex> lock(m_lock);
        m_stopTimer = true;
        m_cv.notify_one();
    }

    // wait for timer thread to finish
    m_timerThread.join();
    return true;
}

bool ELogTimedFlushPolicy::shouldFlush(uint32_t msgSizeBytes) {
    // get timestamp
    Timestamp now = std::chrono::steady_clock::now();

    // compare with previous flush time
    Timestamp prev = m_prevFlushTime.load(std::memory_order_relaxed);
    if (getTimeDiff(now, prev) > m_logTimeLimitMillis) {
        // the one that sets the new flush time will notify caller to flush
        if (m_prevFlushTime.compare_exchange_strong(prev, now, std::memory_order_seq_cst)) {
            return true;
        }
    }
    return false;
}

void ELogTimedFlushPolicy::onTimer() {
    while (!shouldStop()) {
        // wait for timeout or for stop flag to be raised
        {
            std::unique_lock<std::mutex> lock(m_lock);
            m_cv.wait_for(lock, std::chrono::milliseconds(m_logTimeLimitMillis),
                          [this] { return m_stopTimer; });
            if (m_stopTimer) {
                break;
            }
        }

        // we participate with the rest of the concurrent loggers as a phantom logger, so that we
        // avoid duplicate flushes (others call shouldFlush() with some payload size)
        if (shouldFlush(0)) {
            getLogTarget()->flush();
        }
    }
}

bool ELogTimedFlushPolicy::shouldStop() {
    std::unique_lock<std::mutex> lock(m_lock);
    return m_stopTimer;
}

}  // namespace elog
