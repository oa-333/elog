#include "elog_flush_policy.h"

#include <unordered_map>

#include "elog_common.h"
#include "elog_config_loader.h"
#include "elog_error.h"
#include "elog_target.h"

namespace elog {

ELOG_IMPLEMENT_FLUSH_POLICY(ELogAndFlushPolicy);
ELOG_IMPLEMENT_FLUSH_POLICY(ELogOrFlushPolicy);
ELOG_IMPLEMENT_FLUSH_POLICY(ELogNotFlushPolicy);
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

bool ELogFlushPolicy::loadIntFlushPolicy(const ELogConfigMapNode* flushPolicyCfg,
                                         const char* filterName, const char* propName,
                                         uint64_t& value) {
    bool found = false;
    int64_t count = 0;
    if (!flushPolicyCfg->getIntValue(propName, found, count)) {
        ELOG_REPORT_ERROR("Failed to configure %s flush policy (context: %s)", filterName,
                          flushPolicyCfg->getFullContext());
        return false;
    }
    if (!found) {
        ELOG_REPORT_ERROR("Invalid flush policy configuration, missing %s property (context: %s)",
                          propName, flushPolicyCfg->getFullContext());
        return false;
    }
    value = (uint64_t)count;
    return true;
}

bool ELogFlushPolicy::loadIntFlushPolicy(const ELogExpression* expr, const char* filterName,
                                         uint64_t& value) {
    if (expr->m_type != ELogExpressionType::ET_OP_EXPR) {
        ELOG_REPORT_ERROR(
            "Invalid expression type, operator expression required for loading %s filter",
            filterName);
        return false;
    }
    const ELogOpExpression* opExpr = (const ELogOpExpression*)expr;
    if (opExpr->m_op.compare("==") != 0) {
        ELOG_REPORT_ERROR("Invalid comparison operator '%s' for %s filter, only '==' is allowed",
                          opExpr->m_op.c_str(), filterName);
        return false;
    }
    if (!parseIntProp("", "", opExpr->m_rhs, value, false)) {
        ELOG_REPORT_ERROR("Invalid expression operand '%s' for %s filter, required integer type",
                          opExpr->m_rhs.c_str(), filterName);
        return false;
    }
    return true;
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

bool ELogCompoundFlushPolicy::load(const ELogConfigMapNode* flushPolicyCfg) {
    // we expect to find a nested property 'flush_policy_args' with one or more array item
    const ELogConfigValue* cfgValue = flushPolicyCfg->getValue("flush_policy_args");
    if (cfgValue == nullptr) {
        ELOG_REPORT_ERROR(
            "Missing 'flush_policy_args' property required for compound flush policy (context: %s)",
            flushPolicyCfg->getFullContext());
        return false;
    }

    // expected array type
    if (cfgValue->getValueType() != ELogConfigValueType::ELOG_CONFIG_ARRAY_VALUE) {
        ELOG_REPORT_ERROR(
            "Invalid 'flush_policy_args' property type for compound flush policy, expecting array, "
            "seeing instead %s (context: %s)",
            configValueTypeToString(cfgValue->getValueType()), cfgValue->getFullContext());
        return false;
    }
    const ELogConfigArrayNode* arrayNode = ((const ELogConfigArrayValue*)cfgValue)->getArrayNode();

    for (size_t i = 0; i < arrayNode->getValueCount(); ++i) {
        const ELogConfigValue* value = arrayNode->getValueAt(i);
        if (value->getValueType() != ELogConfigValueType::ELOG_CONFIG_MAP_VALUE) {
            ELOG_REPORT_ERROR(
                "Invalid flush policy configuration value type, expecting map, seeing instead %s",
                configValueTypeToString(value->getValueType()), value->getFullContext());
            return false;
        }
        const ELogConfigMapNode* subFlushPolicyCfg =
            ((const ELogConfigMapValue*)value)->getMapNode();
        bool result = true;
        ELogFlushPolicy* flushPolicy =
            ELogConfigLoader::loadFlushPolicy(subFlushPolicyCfg, false, result);
        if (!result) {
            ELOG_REPORT_ERROR(
                "Failed to load %zuth sub-flush-policy for compound flush policy: %s (see previous "
                "errors)",
                i, subFlushPolicyCfg->getFullContext());
            return false;
        }
        if (flushPolicy == nullptr) {
            ELOG_REPORT_ERROR(
                "Failed to load %zuth sub-flush-policy for compound flush policy, policy "
                "specification not found: %s",
                i, subFlushPolicyCfg->getFullContext());
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

bool ELogNotFlushPolicy::load(const std::string& logTargetCfg,
                              const ELogTargetNestedSpec& logTargetSpec) {
    // we expect to find a nested property 'filter_args' with one array item
    ELogTargetNestedSpec::SubSpecMap::const_iterator itr =
        logTargetSpec.m_subSpec.find("flush_policy_args");
    if (itr == logTargetSpec.m_subSpec.end()) {
        ELOG_REPORT_ERROR(
            "Missing 'flush_policy_args' nested property required for NOT flush policy: %s",
            logTargetCfg.c_str());
        return false;
    }

    const ELogTargetNestedSpec::SubSpecList& subSpecList = itr->second;
    if (subSpecList.empty()) {
        ELOG_REPORT_ERROR(
            "Nested property 'flush_policy_args' (required for NOT flush policy) is empty: %s",
            logTargetCfg.c_str());
        return false;
    }
    if (subSpecList.size() > 1) {
        ELOG_REPORT_ERROR(
            "Nested property 'flush_policy_args' (required for NOT flush policy) has more than one "
            "item: %s",
            logTargetCfg.c_str());
        return false;
    }
    const ELogTargetNestedSpec& subSpec = subSpecList[0];
    bool result = false;
    m_flushPolicy = ELogConfigLoader::loadFlushPolicy(logTargetCfg, logTargetSpec, false, result);
    if (!result) {
        ELOG_REPORT_ERROR(
            "Failed to load sub-flush policy for NOT flush policy: %s (see errors above)",
            logTargetCfg.c_str());
        return false;
    }
    if (m_flushPolicy == nullptr) {
        ELOG_REPORT_ERROR(
            "Failed to load sub-flush policy for NOT flush policy, flush policy specification not "
            "found: %s",
            logTargetCfg.c_str());
        return false;
    }
    return true;
}

bool ELogNotFlushPolicy::load(const ELogConfigMapNode* filterCfg) {
    // we expect to find a nested property 'args' with one array item
    const ELogConfigValue* cfgValue = filterCfg->getValue("args");
    if (cfgValue == nullptr) {
        ELOG_REPORT_ERROR("Missing 'args' property required for NOT flush policy (context: %s)",
                          filterCfg->getFullContext());
        return false;
    }

    // expected array type
    if (cfgValue->getValueType() != ELogConfigValueType::ELOG_CONFIG_ARRAY_VALUE) {
        ELOG_REPORT_ERROR(
            "Invalid 'args' property type for NOT flush policy, expecting array, seeing instead %s "
            "(context: %s)",
            configValueTypeToString(cfgValue->getValueType()), cfgValue->getFullContext());
        return false;
    }
    const ELogConfigArrayNode* arrayNode = ((const ELogConfigArrayValue*)cfgValue)->getArrayNode();

    if (arrayNode->getValueCount() == 0) {
        ELOG_REPORT_ERROR(
            "Nested property 'args' (required for NOT flush policy) is empty (context: %s)",
            arrayNode->getFullContext());
        return false;
    }
    if (arrayNode->getValueCount() > 1) {
        ELOG_REPORT_ERROR(
            "Nested property 'args' (required for NOT flush policy) has more than one item "
            "(context: %s)",
            arrayNode->getFullContext());
        return false;
    }
    if (arrayNode->getValueAt(0)->getValueType() != ELogConfigValueType::ELOG_CONFIG_MAP_VALUE) {
        ELOG_REPORT_ERROR(
            "Invalid array property 'args' item type (required for NOT flush policy), expecting "
            "map, seeing instead %s (context: %s)",
            arrayNode->getFullContext());
        return false;
    }
    const ELogConfigMapNode* subFilterCfg =
        ((const ELogConfigMapValue*)arrayNode->getValueAt(0))->getMapNode();
    bool result = false;
    m_flushPolicy = ELogConfigLoader::loadFlushPolicy(subFilterCfg, false, result);
    if (!result) {
        ELOG_REPORT_ERROR("Failed to load sub-flush policy for NOT flush policy (context: %s)",
                          subFilterCfg->getFullContext());
        return false;
    }
    if (m_flushPolicy == nullptr) {
        ELOG_REPORT_ERROR(
            "Failed to load sub-flush policy for NOT flush policy, flush policy specification not "
            "found (context: %s)",
            subFilterCfg->getFullContext());
        return false;
    }
    return true;
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

bool ELogCountFlushPolicy::load(const ELogConfigMapNode* flushPolicyCfg) {
    return loadIntFlushPolicy(flushPolicyCfg, "count", "flush_count", m_logCountLimit);
}

bool ELogCountFlushPolicy::load(const ELogExpression* expr) {
    return loadIntFlushPolicy(expr, "count", m_logCountLimit);
}

bool ELogCountFlushPolicy::shouldFlush(uint32_t msgSizeBytes) {
    uint64_t logCount = m_currentLogCount.fetch_add(1, std::memory_order_relaxed);
    return ((logCount + 1) % m_logCountLimit == 0);
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

bool ELogSizeFlushPolicy::load(const ELogConfigMapNode* flushPolicyCfg) {
    return loadIntFlushPolicy(flushPolicyCfg, "size", "flush_size_bytes", m_logSizeLimitBytes);
}

bool ELogSizeFlushPolicy::load(const ELogExpression* expr) {
    return loadIntFlushPolicy(expr, "size", m_logSizeLimitBytes);
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

bool ELogTimedFlushPolicy::load(const ELogConfigMapNode* flushPolicyCfg) {
    bool found = false;
    uint64_t timeoutMillis = 0;
    if (!loadIntFlushPolicy(flushPolicyCfg, "timed", "flush_timeout_millis", timeoutMillis)) {
        return false;
    }
    m_logTimeLimitMillis = Millis(timeoutMillis);
    return true;
}

bool ELogTimedFlushPolicy::load(const ELogExpression* expr) {
    uint64_t timeoutMillis = 0;
    if (!loadIntFlushPolicy(expr, "timed", timeoutMillis)) {
        return false;
    }
    m_logTimeLimitMillis = Millis(timeoutMillis);
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
