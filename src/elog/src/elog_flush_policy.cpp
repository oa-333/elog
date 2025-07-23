#include "elog_flush_policy.h"

#include <unordered_map>

#include "elog.h"
#include "elog_common.h"
#include "elog_config_loader.h"
#include "elog_report.h"
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
ELOG_IMPLEMENT_FLUSH_POLICY(ELogChainedFlushPolicy)
ELOG_IMPLEMENT_FLUSH_POLICY(ELogGroupFlushPolicy)

/** @def The maximum number of flush policies types that can be defined in the system. */
#define ELOG_MAX_FLUSH_POLICY_COUNT 100

// TODO: have this configurable in the flush policy
/** @def The default maximum number of threads used for group flush policy resource allocation. */
#define ELOG_MAX_THREAD_COUNT 4096

// TODO: have this configurable in the flush policy
/** @def The default group flush GC recycling frequency (once every X retire calls). */
#define ELOG_FLUSH_GC_FREQ 4096

// Group flush GC tracer
#ifdef ELOG_ENABLE_GROUP_FLUSH_GC_TRACE
static ELogLogger* sGCLogger = nullptr;

#define ELOG_GROUP_FLUSH_GC_TRACE_BUFFER_SIZE 2000000

static void initGCLogger() {
    ELogTargetId id = elog::addTracer("./gc_trace.log", ELOG_GROUP_FLUSH_GC_TRACE_BUFFER_SIZE,
                                      "trace", "group-flush-gc");
    sGCLogger = elog::getSharedLogger("group-flush-gc");
}

static void resetGCLogger() { sGCLogger = nullptr; }

static ELogLogger* getGCTraceLogger() {
    // init once
    if (sGCLogger == nullptr) {
        initGCLogger();
    }
    return sGCLogger;
}
#endif

// implement flush policy factory by name with static registration
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

void ELogFlushPolicy::moderateFlush(ELogTarget* logTarget) { logTarget->flush(); }

bool ELogFlushPolicy::loadIntFlushPolicy(const ELogConfigMapNode* flushPolicyCfg,
                                         const char* flushPolicyName, const char* propName,
                                         uint64_t& value) {
    bool found = false;
    int64_t count = 0;
    if (!flushPolicyCfg->getIntValue(propName, found, count)) {
        ELOG_REPORT_ERROR("Failed to configure %s flush policy (context: %s)", flushPolicyName,
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

bool ELogFlushPolicy::loadTimeoutFlushPolicy(const ELogConfigMapNode* flushPolicyCfg,
                                             const char* flushPolicyName, const char* propName,
                                             uint64_t& value, ELogTimeoutUnits targetUnits) {
    bool found = false;
    std::string strValue;
    if (!flushPolicyCfg->getStringValue(propName, found, strValue)) {
        ELOG_REPORT_ERROR("Failed to configure %s flush policy (context: %s)", flushPolicyName,
                          flushPolicyCfg->getFullContext());
        return false;
    }
    if (!found) {
        ELOG_REPORT_ERROR("Invalid flush policy configuration, missing %s property (context: %s)",
                          propName, flushPolicyCfg->getFullContext());
        return false;
    }
    return parseTimeoutProp(propName, "", strValue, value, targetUnits);
}

bool ELogFlushPolicy::loadSizeFlushPolicy(const ELogConfigMapNode* flushPolicyCfg,
                                          const char* flushPolicyName, const char* propName,
                                          uint64_t& value, ELogSizeUnits targetUnits) {
    bool found = false;
    std::string strValue;
    if (!flushPolicyCfg->getStringValue(propName, found, strValue)) {
        ELOG_REPORT_ERROR("Failed to configure %s flush policy (context: %s)", flushPolicyName,
                          flushPolicyCfg->getFullContext());
        return false;
    }
    if (!found) {
        ELOG_REPORT_ERROR("Invalid flush policy configuration, missing %s property (context: %s)",
                          propName, flushPolicyCfg->getFullContext());
        return false;
    }
    return parseSizeProp(propName, "", strValue, value, targetUnits);
}

bool ELogFlushPolicy::loadIntFlushPolicy(const ELogExpression* expr, const char* flushPolicyName,
                                         uint64_t& value, const char* propName /* = nullptr */) {
    if (expr->m_type != ELogExpressionType::ET_OP_EXPR) {
        ELOG_REPORT_ERROR(
            "Invalid expression type, operator expression required for loading %s flush policy "
            "(property: %s)",
            flushPolicyName, propName ? propName : flushPolicyName);
        return false;
    }
    const ELogOpExpression* opExpr = (const ELogOpExpression*)expr;
    if (opExpr->m_op.compare("==") != 0 && opExpr->m_op.compare(":") != 0) {
        ELOG_REPORT_ERROR(
            "Invalid comparison operator '%s' for %s flush policy, only '==' or ':' is allowed in "
            "this context (property: %s)",
            opExpr->m_op.c_str(), flushPolicyName, propName ? propName : flushPolicyName);
        return false;
    }
    if (!parseIntProp("", "", opExpr->m_rhs, value, false)) {
        ELOG_REPORT_ERROR(
            "Invalid expression operand '%s' for %s flush policy, required integer type (property: "
            "%s)",
            opExpr->m_rhs.c_str(), flushPolicyName, propName ? propName : flushPolicyName);
        return false;
    }
    return true;
}

bool ELogFlushPolicy::loadTimeoutFlushPolicy(const ELogExpression* expr,
                                             const char* flushPolicyName, uint64_t& value,
                                             ELogTimeoutUnits targetUnits,
                                             const char* propName /* = nullptr */) {
    if (expr->m_type != ELogExpressionType::ET_OP_EXPR) {
        ELOG_REPORT_ERROR(
            "Invalid expression type, operator expression required for loading %s flush policy "
            "(property: %s)",
            flushPolicyName, propName ? propName : flushPolicyName);
        return false;
    }
    const ELogOpExpression* opExpr = (const ELogOpExpression*)expr;
    if (opExpr->m_op.compare("==") != 0 && opExpr->m_op.compare(":") != 0) {
        ELOG_REPORT_ERROR(
            "Invalid comparison operator '%s' for %s flush policy, only '==' or ':' is allowed in "
            "this context (property: %s)",
            opExpr->m_op.c_str(), flushPolicyName, propName ? propName : flushPolicyName);
        return false;
    }
    if (!parseTimeoutProp(propName, "", opExpr->m_rhs, value, targetUnits, false)) {
        ELOG_REPORT_ERROR(
            "Invalid expression operand '%s' for %s flush policy, required timeout type (property: "
            "%s)",
            opExpr->m_rhs.c_str(), flushPolicyName, propName ? propName : flushPolicyName);
        return false;
    }
    return true;
}

bool ELogFlushPolicy::loadSizeFlushPolicy(const ELogExpression* expr, const char* flushPolicyName,
                                          uint64_t& value, ELogSizeUnits targetUnits,
                                          const char* propName /* = nullptr */) {
    if (expr->m_type != ELogExpressionType::ET_OP_EXPR) {
        ELOG_REPORT_ERROR(
            "Invalid expression type, operator expression required for loading %s flush policy "
            "(property: %s)",
            flushPolicyName, propName ? propName : flushPolicyName);
        return false;
    }
    const ELogOpExpression* opExpr = (const ELogOpExpression*)expr;
    if (opExpr->m_op.compare("==") != 0 && opExpr->m_op.compare(":") != 0) {
        ELOG_REPORT_ERROR(
            "Invalid comparison operator '%s' for %s flush policy, only '==' or ':' is allowed in "
            "this context (property: %s)",
            opExpr->m_op.c_str(), flushPolicyName, propName ? propName : flushPolicyName);
        return false;
    }
    if (!parseSizeProp(propName, "", opExpr->m_rhs, value, targetUnits, false)) {
        ELOG_REPORT_ERROR(
            "Invalid expression operand '%s' for %s flush policy, required size type (property: "
            "%s)",
            opExpr->m_rhs.c_str(), flushPolicyName, propName ? propName : flushPolicyName);
        return false;
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

void ELogCompoundFlushPolicy::propagateLogTarget(ELogTarget* logTarget) {
    for (uint32_t i = 0; i < m_flushPolicies.size(); ++i) {
        // this will get propagated to all active sub-policies
        if (m_flushPolicies[i]->isActive()) {
            m_flushPolicies[i]->setLogTarget(getLogTarget());
        }
    }
}

bool ELogCompoundFlushPolicy::loadCompositeExpr(const ELogCompositeExpression* expr) {
    for (ELogExpression* subExpr : expr->m_expressions) {
        ELogFlushPolicy* subFlushPolicy = ELogConfigLoader::loadFlushPolicyExpr(subExpr);
        if (subFlushPolicy == nullptr) {
            ELOG_REPORT_ERROR("Failed to load sub-flush policy from expression");
            return false;
        }
        addFlushPolicy(subFlushPolicy);
    }
    return true;
}

bool ELogAndFlushPolicy::loadExpr(const ELogExpression* expr) {
    if (expr->m_type != ELogExpressionType::ET_AND_EXPR) {
        ELOG_REPORT_ERROR("Cannot load AND flush policy from expression, invalid expression type");
        return false;
    }
    return loadCompositeExpr((const ELogCompositeExpression*)expr);
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

bool ELogOrFlushPolicy::loadExpr(const ELogExpression* expr) {
    if (expr->m_type != ELogExpressionType::ET_OR_EXPR) {
        ELOG_REPORT_ERROR("Cannot load OR flush policy from expression, invalid expression type");
        return false;
    }
    return loadCompositeExpr((const ELogCompositeExpression*)expr);
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
    return res;
}

bool ELogNotFlushPolicy::load(const ELogConfigMapNode* flushPolicyCfg) {
    // we expect to find a nested property 'args' with one array item
    const ELogConfigValue* cfgValue = flushPolicyCfg->getValue("flush_policy_args");
    if (cfgValue == nullptr) {
        ELOG_REPORT_ERROR(
            "Missing 'flush_policy_args' property required for NOT flush policy (context: %s)",
            flushPolicyCfg->getFullContext());
        return false;
    }

    // expected array type
    if (cfgValue->getValueType() != ELogConfigValueType::ELOG_CONFIG_ARRAY_VALUE) {
        ELOG_REPORT_ERROR(
            "Invalid 'flush_policy_args' property type for NOT flush policy, expecting array, "
            "seeing instead %s (context: %s)",
            configValueTypeToString(cfgValue->getValueType()), cfgValue->getFullContext());
        return false;
    }
    const ELogConfigArrayNode* arrayNode = ((const ELogConfigArrayValue*)cfgValue)->getArrayNode();

    if (arrayNode->getValueCount() == 0) {
        ELOG_REPORT_ERROR(
            "Nested property 'flush_policy_args' (required for NOT flush policy) is empty "
            "(context: %s)",
            arrayNode->getFullContext());
        return false;
    }
    if (arrayNode->getValueCount() > 1) {
        ELOG_REPORT_ERROR(
            "Nested property 'flush_policy_args' (required for NOT flush policy) has more than one "
            "item (context: %s)",
            arrayNode->getFullContext());
        return false;
    }
    if (arrayNode->getValueAt(0)->getValueType() != ELogConfigValueType::ELOG_CONFIG_MAP_VALUE) {
        ELOG_REPORT_ERROR(
            "Invalid array property 'flush_policy_args' item type (required for NOT flush policy), "
            "expecting map, seeing instead %s (context: %s)",
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

bool ELogNotFlushPolicy::loadExpr(const ELogExpression* expr) {
    if (expr->m_type != ELogExpressionType::ET_NOT_EXPR) {
        ELOG_REPORT_ERROR("Cannot load NOT flush policy from expression, invalid expression type");
        return false;
    }
    ELogNotExpression* notExpr = (ELogNotExpression*)expr;
    m_flushPolicy = ELogConfigLoader::loadFlushPolicyExpr(notExpr->m_expression);
    if (m_flushPolicy == nullptr) {
        ELOG_REPORT_ERROR("Failed to load sub-flush policy for NOT flush policy");
        return false;
    }
    return true;
}

bool ELogImmediateFlushPolicy::shouldFlush(uint32_t msgSizeBytes) { return true; }

bool ELogNeverFlushPolicy::shouldFlush(uint32_t msgSizeBytes) { return false; }

bool ELogCountFlushPolicy::load(const ELogConfigMapNode* flushPolicyCfg) {
    return loadIntFlushPolicy(flushPolicyCfg, "count", "flush_count", m_logCountLimit);
}

bool ELogCountFlushPolicy::loadExpr(const ELogExpression* expr) {
    return loadIntFlushPolicy(expr, "count", m_logCountLimit);
}

bool ELogCountFlushPolicy::shouldFlush(uint32_t msgSizeBytes) {
    uint64_t logCount = m_currentLogCount.fetch_add(1, std::memory_order_relaxed);
    return ((logCount + 1) % m_logCountLimit == 0);
}

bool ELogSizeFlushPolicy::load(const ELogConfigMapNode* flushPolicyCfg) {
    return loadSizeFlushPolicy(flushPolicyCfg, "size", "flush_size", m_logSizeLimitBytes,
                               ELogSizeUnits::SU_BYTES);
}

bool ELogSizeFlushPolicy::loadExpr(const ELogExpression* expr) {
    return loadSizeFlushPolicy(expr, "size", m_logSizeLimitBytes, ELogSizeUnits::SU_BYTES);
}

bool ELogSizeFlushPolicy::shouldFlush(uint32_t msgSizeBytes) {
    uint64_t prevSizeBytes =
        m_currentLogSizeBytes.fetch_add(msgSizeBytes, std::memory_order_relaxed);
    uint64_t currSizeBytes = prevSizeBytes + msgSizeBytes;
    return (currSizeBytes / m_logSizeLimitBytes) > (prevSizeBytes / m_logSizeLimitBytes);
}

ELogTimedFlushPolicy::ELogTimedFlushPolicy()
    : ELogFlushPolicy(true),
      m_logTimeLimitMillis(0),
      m_prevFlushTime(getTimestamp()),
      m_stopTimer(false) {}

ELogTimedFlushPolicy::ELogTimedFlushPolicy(uint64_t logTimeLimitMillis, ELogTarget* logTarget)
    : ELogFlushPolicy(true),
      m_logTimeLimitMillis(logTimeLimitMillis),
      m_prevFlushTime(getTimestamp()),
      m_stopTimer(false) {}

ELogTimedFlushPolicy::~ELogTimedFlushPolicy() {}

bool ELogTimedFlushPolicy::load(const ELogConfigMapNode* flushPolicyCfg) {
    return loadTimeoutFlushPolicy(flushPolicyCfg, "time", "flush_timeout", m_logTimeLimitMillis,
                                  ELogTimeoutUnits::TU_MILLI_SECONDS);
}

bool ELogTimedFlushPolicy::loadExpr(const ELogExpression* expr) {
    return loadTimeoutFlushPolicy(expr, "time", m_logTimeLimitMillis,
                                  ELogTimeoutUnits::TU_MILLI_SECONDS);
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
    ELogTime now = getTimestamp();

    // compare with previous flush time
    ELogTime prev = m_prevFlushTime.load(std::memory_order_relaxed);
    if (getTimeDiffMillis(now, prev) > m_logTimeLimitMillis) {
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

bool ELogChainedFlushPolicy::load(const ELogConfigMapNode* flushPolicyCfg) {
    // we expect to find two nested properties 'control_flush_policy', and 'moderate_flush_policy'
    m_controlPolicy = loadSubFlushPolicy("control", "control_flush_policy", flushPolicyCfg);
    if (m_controlPolicy == nullptr) {
        return false;
    }
    m_moderatePolicy = loadSubFlushPolicy("moderate", "moderate_flush_policy", flushPolicyCfg);
    if (m_moderatePolicy == nullptr) {
        delete m_controlPolicy;
        m_controlPolicy = nullptr;
        return false;
    }
    return false;
}

bool ELogChainedFlushPolicy::loadExpr(const ELogExpression* expr) {
    if (expr->m_type != ELogExpressionType::ET_CHAIN_EXPR) {
        ELOG_REPORT_ERROR(
            "Cannot load CHAIN flush policy from expression, invalid expression type");
        return false;
    }
    ELogChainExpression* chainExpr = (ELogChainExpression*)expr;
    if (chainExpr->m_expressions.size() != 2) {
        ELOG_REPORT_ERROR("Invalid CHAIN expression, exactly two sub-expressions are expected");
        return false;
    }
    ELogFlushPolicy* controlPolicy =
        ELogConfigLoader::loadFlushPolicyExpr(chainExpr->m_expressions[0]);
    if (controlPolicy == nullptr) {
        ELOG_REPORT_ERROR("Failed to load control flush policy for CHAIN flush policy");
        return false;
    }
    ELogFlushPolicy* moderatePolicy =
        ELogConfigLoader::loadFlushPolicyExpr(chainExpr->m_expressions[1]);
    if (moderatePolicy == nullptr) {
        ELOG_REPORT_ERROR("Failed to load moderate flush policy for CHAIN flush policy");
        delete controlPolicy;
        return false;
    }
    setControlFlushPolicy(controlPolicy);
    setModerateFlushPolicy(moderatePolicy);
    if (controlPolicy->isActive() || moderatePolicy->isActive()) {
        // mark ourselves as active policy in case any of sub-policies is active
        setActive();
    }
    return true;
}

bool ELogChainedFlushPolicy::start() {
    if (!m_controlPolicy->start()) {
        ELOG_REPORT_ERROR("Failed to start control policy");
        return false;
    }
    if (!m_moderatePolicy->start()) {
        ELOG_REPORT_ERROR("Failed to start moderate policy");
        return false;
    }
    return true;
}

bool ELogChainedFlushPolicy::stop() {
    if (!m_moderatePolicy->stop()) {
        ELOG_REPORT_ERROR("Failed to stop moderate policy");
        return false;
    }
    if (!m_controlPolicy->stop()) {
        ELOG_REPORT_ERROR("Failed to stop control policy");
        return false;
    }
    return true;
}

ELogFlushPolicy* ELogChainedFlushPolicy::loadSubFlushPolicy(
    const char* typeName, const char* propName, const ELogConfigMapNode* flushPolicyCfg) {
    const ELogConfigValue* cfgValue = flushPolicyCfg->getValue(propName);
    if (cfgValue == nullptr) {
        ELOG_REPORT_ERROR("Missing '%s' property required for CHAIN flush policy (context: %s)",
                          propName, flushPolicyCfg->getFullContext());
        return nullptr;
    }

    // expected map type
    if (cfgValue->getValueType() != ELogConfigValueType::ELOG_CONFIG_MAP_VALUE) {
        ELOG_REPORT_ERROR(
            "Invalid '%s' property type for CHAIN flush policy, expecting map, seeing instead %s "
            "(context: %s)",
            propName, configValueTypeToString(cfgValue->getValueType()),
            cfgValue->getFullContext());
        return nullptr;
    }

    bool result = false;
    const ELogConfigMapNode* mapNode = ((const ELogConfigMapValue*)cfgValue)->getMapNode();
    ELogFlushPolicy* flushPolicy = ELogConfigLoader::loadFlushPolicy(mapNode, false, result);
    if (!result) {
        ELOG_REPORT_ERROR(
            "Failed to load %s flush-policy for CHAIN flush policy: %s (see previous errors)",
            typeName, mapNode->getFullContext());
        return nullptr;
    }
    if (flushPolicy == nullptr) {
        ELOG_REPORT_ERROR(
            "Failed to load %s sub-flush-policy for CHAIN flush policy, policy "
            "specification not found: %s",
            typeName, mapNode->getFullContext());
        return nullptr;
    }
    return flushPolicy;
}

void ELogChainedFlushPolicy::propagateLogTarget(ELogTarget* logTarget) {
    if (m_controlPolicy->isActive()) {
        m_controlPolicy->setLogTarget(logTarget);
    }
    if (m_moderatePolicy->isActive()) {
        m_moderatePolicy->setLogTarget(logTarget);
    }
}

bool ELogGroupFlushPolicy::load(const ELogConfigMapNode* flushPolicyCfg) {
    uint64_t groupSize = 0;
    if (!loadIntFlushPolicy(flushPolicyCfg, "group", "size", groupSize)) {
        return false;
    }
    m_groupSize = (uint32_t)groupSize;

    uint64_t groupTimeoutMicros = 0;
    if (!loadTimeoutFlushPolicy(flushPolicyCfg, "group", "timeout", groupTimeoutMicros,
                                ELogTimeoutUnits::TU_MICRO_SECONDS)) {
        return false;
    }
    m_groupTimeoutMicros = (Micros)groupTimeoutMicros;
    return true;
}

bool ELogGroupFlushPolicy::loadExpr(const ELogExpression* expr) {
    if (expr->m_type != ELogExpressionType::ET_FUNC_EXPR) {
        ELOG_REPORT_ERROR(
            "Cannot load group flush policy, invalid expression type (required function "
            "expression)");
        return false;
    }
    const ELogFunctionExpression* funcExpr = (const ELogFunctionExpression*)expr;
    if (funcExpr->m_expressions.size() != 2) {
        ELOG_REPORT_ERROR(
            "Cannot load group flush policy, function expression must contain exactly two "
            "sub-expressions");
        return false;
    }
    if (!loadIntFlushPolicy(funcExpr->m_expressions[0], "group", m_groupSize, "size")) {
        return false;
    }
    uint64_t groupTimeoutMicros = 0;
    if (!loadTimeoutFlushPolicy(funcExpr->m_expressions[1], "group", groupTimeoutMicros,
                                ELogTimeoutUnits::TU_MICRO_SECONDS, "timeout")) {
        return false;
    }
    m_groupTimeoutMicros = Micros(groupTimeoutMicros);
    return true;
}

bool ELogGroupFlushPolicy::start() {
    // initialize the private garbage collector
    if (!m_gc.initialize("Group-flush-policy GC", ELOG_MAX_THREAD_COUNT, ELOG_FLUSH_GC_FREQ)) {
        ELOG_REPORT_ERROR("Failed to initialize private garbage collector for group flush policy");
        return false;
    }
#ifdef ELOG_ENABLE_GROUP_FLUSH_GC_TRACE
    sGCLogger = getGCTraceLogger();
    m_gc.setTraceLogger(sGCLogger);
#endif
    return true;
}

bool ELogGroupFlushPolicy::stop() {
    // recycle all open groups (note: trace logger is still used inside GC)
    if (!m_gc.destroy()) {
        ELOG_REPORT_ERROR("Failed to destroy private garbage collector for group flush policy");
        return false;
    }
#ifdef ELOG_ENABLE_GROUP_FLUSH_GC_TRACE
    resetGCLogger();
#endif
    return true;
}

bool ELogGroupFlushPolicy::shouldFlush(uint32_t msgSizeBytes) {
    // this is a moderating flush policy, so we always return true
    return true;
}

void ELogGroupFlushPolicy::moderateFlush(ELogTarget* logTarget) {
    // NOTE: we may arrive here concurrently from many threads
    // so we need to do one of the following:
    // (1) if no group is present, then open new group as leader
    // (2) if a group is present the join it as follower and wait
    // (3) if a group is present but is already closed we may either wait or we can open a new group
    //
    // when opening a new group, the group leader blocks until group is full or timeout expires
    // each group should be guarded by lock/cv for easier implementation, and avoiding leader doing
    // busy wait. Each follower that joins increments group size, and waits on CV. If size limit
    // reached, the cv is notified, then the leader wakes up (do we need separate CV?), performs
    // flush, and notifies all group members flush is done. We can use single CV with different
    // group events - GROUP_CLOSE, GROUP_FLUSH_DONE.
    //
    // for this we need a group object that is already accessible. If using pointers and group
    // objects that may abruptly change (see below), then accessing the group pointer may be tricky.
    // the same race occurs with leader and subsequent followers.

    // the last use case requires more attention. If we wait for group flush until we open the next
    // group, then performance will not be good. if we open new group while another is already open,
    // then we have a race condition, because many groups may be open at the same time, where all
    // but the last one are calling flush (maybe even in parallel...).

    // the general approach for all race conditions is as follows:
    // use a single atomic pointer for current group.
    // when each flush request arrives the following logic is used:
    // 1. if the pointer is null then allocate a new group as leader and attempt to CAS pointer
    // 2. if CAS succeeded then current thread is leader, and it waits for size or timeout.
    // 3. if CAS failed, then we attempt to join the current group
    // 4. if join succeeded we delete the group from step 1 if any such was created, then we
    // increment the group size and wait for flush.
    // 5. if join failed (group closed, now being flushed), we attempt to CAS again the current
    // group, with the one we already created, and we essentially go back to step 3.
    // 6. finally when leader wakes up, either due to size or timeout, it closes the group, and
    // start flush, then it notifies all group members flush is done. When all members have left,
    // the leader deletes the group, and tries to CAS the group pointer back to null.
    // 7. followers wait for flush and when done they leave. The last one to leave (use atomic
    // counter), notifies the leader all members left, so he can safely delete the group.

    // NOTE: since leader deletes the group, there is a race here that can lead to crash
    // either use GC, hazard pointers or use a lock

    // we increment the epoch for each flush request
    uint64_t epoch = m_epoch.fetch_add(1, std::memory_order_acquire);
    m_gc.beginEpoch(epoch);
    bool hasGroup = false;
    bool isLeader = false;
    Group* currentGroup = nullptr;
    Group* newGroup = nullptr;
    while (!hasGroup) {
        currentGroup = m_currentGroup.load(std::memory_order_relaxed);
        if (currentGroup != nullptr && currentGroup->join()) {
            hasGroup = true;
#ifdef ELOG_ENABLE_GROUP_FLUSH_GC_TRACE
            ELOG_INFO_EX(sGCLogger, "Joined group %p as follower, epoch %" PRIu64, currentGroup,
                         epoch);
#endif
        } else {
            // either no group, or group is full/closed, so try to form a new group
            // since there might be several iterations, we create new group only once
            if (newGroup == nullptr) {
                newGroup = new (std::nothrow) Group(logTarget, m_groupSize, m_groupTimeoutMicros);
                if (newGroup == nullptr) {
                    ELOG_REPORT_ERROR("Failed to allocate new group, out of memory");
                    m_gc.endEpoch(epoch);
                    return;
                }
            }

            // try to set the new group as the current group
            if (m_currentGroup.compare_exchange_strong(currentGroup, newGroup,
                                                       std::memory_order_seq_cst)) {
                // leader, use newGroup
                isLeader = true;
                hasGroup = true;
#ifdef ELOG_ENABLE_GROUP_FLUSH_GC_TRACE
                ELOG_INFO_EX(sGCLogger, "Formed a new group %p, epoch %" PRIu64, newGroup, epoch);
#endif
            }
            // we just lost the race, meaning there is a new group, so try to join it
            // we do it in the next round
        }
    }

    if (isLeader) {
        // execute leader code
        newGroup->execLeader();
#ifdef ELOG_ENABLE_GROUP_FLUSH_GC_TRACE
        ELOG_INFO_EX(sGCLogger, "Finished executing leader code on group %p, epoch %" PRIu64,
                     newGroup, epoch);
#endif

        // try to put current group to null (there might be other threads trying to do so)
        // NOTE: the following call might change the value of newGroup, and therefore we first save
        // it in another variable (otherwise this leads to crashes)
        Group* retiredGroup = newGroup;
        m_currentGroup.compare_exchange_strong(newGroup, nullptr, std::memory_order_seq_cst);

        // any transaction that begins from this point onward will no longer see newGroup, so we use
        // the current epoch for this purpose
        uint64_t retireEpoch = m_epoch.load(std::memory_order_acquire);
        m_gc.retire(retiredGroup, retireEpoch);
    } else {
        // delete unused new group, and execute follower code
        if (newGroup != nullptr) {
            delete newGroup;
#ifdef ELOG_ENABLE_GROUP_FLUSH_GC_TRACE
            ELOG_INFO_EX(sGCLogger, "Deleting unused new group %p", newGroup);
#endif
        }
        currentGroup->execFollower();
#ifdef ELOG_ENABLE_GROUP_FLUSH_GC_TRACE
        ELOG_INFO_EX(sGCLogger, "Finished executing follower code on group %p, epoch %" PRIu64,
                     currentGroup, epoch);
#endif
        currentGroup = nullptr;
    }
    m_gc.endEpoch(epoch);
}

ELogGroupFlushPolicy::Group::Group(ELogTarget* logTarget, uint64_t groupSize,
                                   Micros groupTimeoutMicros)
    : m_logTarget(logTarget),
      m_groupSize(groupSize),
      m_groupTimeoutMicros(groupTimeoutMicros),
      m_memberCount(1),
      m_state(State::WAIT),
      m_leaderThreadId(getCurrentThreadId()) {}

bool ELogGroupFlushPolicy::Group::join() {
    assert(getCurrentThreadId() != m_leaderThreadId);
    std::unique_lock<std::mutex> lock(m_lock);
    if (m_state != State::WAIT) {
        return false;
    }
    if (++m_memberCount == m_groupSize) {
        m_state = State::FULL;
        m_cv.notify_all();
    }
    return true;
}

void ELogGroupFlushPolicy::Group::execLeader() {
    assert(getCurrentThreadId() == m_leaderThreadId);
    std::unique_lock<std::mutex> lock(m_lock);
    m_cv.wait_for(lock, m_groupTimeoutMicros, [this]() { return m_state == State::FULL; });
    // declare group closed, even if not ll possible members joined
    m_state = State::CLOSED;

    // execute flush
    // NOTE: flush moderation takes place only when the log target is natively thread safe, so the
    // call to flush here is ok, because that would not cause the lock to be taken again
    m_logTarget->flush();

    // notify flush done, and wait for all-left event, but only if there is at least one follower
    if (m_memberCount > 1) {
        m_state = State::FLUSH_DONE;
        m_cv.notify_all();
        m_cv.wait(lock, [this]() { return m_state == State::ALL_LEFT; });
    }
}

void ELogGroupFlushPolicy::Group::execFollower() {
    // now wait until flush done
    assert(getCurrentThreadId() != m_leaderThreadId);
    std::unique_lock<std::mutex> lock(m_lock);
    m_cv.wait(lock, [this]() { return m_state == State::FLUSH_DONE; });
    if (--m_memberCount == 1) {
        // last one to leave (except leader) should notify leader to wrap up
        m_state = State::ALL_LEFT;
        m_cv.notify_all();
    }
}

}  // namespace elog
