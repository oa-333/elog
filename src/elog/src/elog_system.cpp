#include "elog_system.h"

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstring>
#include <mutex>
#include <unordered_map>

#include "elog_buffered_file_target.h"
#include "elog_db_schema_handler.h"
#include "elog_deferred_target.h"
#include "elog_file_schema_handler.h"
#include "elog_file_target.h"
#include "elog_flush_policy.h"
#include "elog_msgq_schema_handler.h"
#include "elog_quantum_target.h"
#include "elog_queued_target.h"
#include "elog_rate_limiter.h"
#include "elog_segmented_file_target.h"
#include "elog_sys_schema_handler.h"
#include "elog_syslog_target.h"

#ifdef ELOG_MINGW
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace elog {

/** @brief Log level configuration used for delayed log level propagation. */
struct ELogLevelCfg {
    ELogSource* m_logSource;
    ELogLevel m_logLevel;
    ELogSource::PropagateMode m_propagationMode;
};

class ELogDefaultErrorHandler : public ELogErrorHandler {
public:
    ELogDefaultErrorHandler() {}
    ~ELogDefaultErrorHandler() final {}

    void onError(const char* msg) final {
        fprintf(stderr, "<ELOG> ERROR: %s\n", msg);
        fflush(stderr);
    }
};

static const char* ELOG_SCHEMA_MARKER = "://";
static const uint32_t ELOG_SCHEMA_LEN = 3;
static const uint32_t ELOG_MAX_SCHEMA = 20;

static ELogDefaultErrorHandler sDefaultErrorHandler;
static ELogErrorHandler* sErrorHandler = nullptr;
static ELogFilter* sGlobalFilter = nullptr;
static std::vector<ELogTarget*> sLogTargets;
static std::atomic<ELogSourceId> sNextLogSourceId;

inline ELogSourceId allocLogSourceId() {
    return sNextLogSourceId.fetch_add(1, std::memory_order_relaxed);
}

static std::mutex sSourceTreeLock;
static ELogSource* sRootLogSource = nullptr;
typedef std::unordered_map<ELogSourceId, ELogSource*> ELogSourceMap;
static ELogSourceMap sLogSourceMap;
static ELogLogger* sDefaultLogger = nullptr;
static ELogTarget* sDefaultLogTarget = nullptr;
static ELogFormatter* sGlobalFormatter = nullptr;
static ELogFlushPolicy* sFlushPolicy = nullptr;

static ELogSchemaHandler* sSchemaHandlers[ELOG_MAX_SCHEMA] = {};
static uint32_t sSchemaHandlerCount = 0;
typedef std::unordered_map<std::string, int> ELogSchemaHandlerMap;
static ELogSchemaHandlerMap sSchemaHandlerMap;

template <typename T>
static bool initSchemaHandler(const char* name) {
    T* handler = new (std::nothrow) T();
    if (handler == nullptr) {
        ELogSystem::reportError("Failed to create %s schema handler, out of memory", name);
        return false;
    }
    if (!ELogSystem::registerSchemaHandler(name, handler)) {
        ELogSystem::reportError("Failed to add %s schema handler", name);
        delete handler;
        return false;
    }
    return true;
}

bool ELogSystem::initSchemaHandlers() {
    if (!initSchemaHandler<ELogSysSchemaHandler>("sys") ||
        !initSchemaHandler<ELogFileSchemaHandler>("file") ||
        !initSchemaHandler<ELogDbSchemaHandler>("db") ||
        !initSchemaHandler<ELogMsgQSchemaHandler>("msgq")) {
        termGlobals();
        return false;
    }
    return true;
}

void ELogSystem::termSchemaHandlers() {
    for (uint32_t i = 0; i < sSchemaHandlerCount; ++i) {
        delete sSchemaHandlers[i];
        sSchemaHandlers[i] = nullptr;
    }
    sSchemaHandlerCount = 0;
    sSchemaHandlerMap.clear();
}

bool ELogSystem::initGlobals() {
    if (!initFieldSelectors()) {
        reportError("Failed to initialize field selectors");
        return false;
    }
    if (!initSchemaHandlers()) {
        reportError("Failed to initialize predefined schema handlers");
        termGlobals();
        return false;
    }

    // root logger has no name
    // NOTE: this is the only place where we cannot use logging macros
    sRootLogSource = new (std::nothrow) ELogSource(allocLogSourceId(), "");
    if (sRootLogSource == nullptr) {
        reportError("Failed to create root log source, out of memory");
        termGlobals();
        return false;
    }

    // add to global map
    bool res =
        sLogSourceMap.insert(ELogSourceMap::value_type(sRootLogSource->getId(), sRootLogSource))
            .second;
    if (!res) {
        reportError("Failed to insert root log source to global source map (duplicate found)");
        termGlobals();
        return false;
    }

    sDefaultLogger = sRootLogSource->createSharedLogger();
    if (sDefaultLogger == nullptr) {
        reportError("Failed to create default logger, out of memory");
        termGlobals();
        return false;
    }
    sDefaultLogTarget = new (std::nothrow) ELogFileTarget(stderr);
    if (sDefaultLogTarget == nullptr) {
        reportError("Failed to create default log target, out of memory");
        termGlobals();
        return false;
    }
    sGlobalFormatter = new (std::nothrow) ELogFormatter();
    if (!sGlobalFormatter->initialize()) {
        reportError("Failed to initialize log formatter");
        termGlobals();
        return false;
    }

    return true;
}

void ELogSystem::termGlobals() {
    for (ELogTarget* logTarget : sLogTargets) {
        if (logTarget != nullptr) {
            logTarget->stop();
            delete logTarget;
        }
    }
    sLogTargets.clear();

    setLogFormatter(nullptr);
    setLogFilter(nullptr);
    if (sDefaultLogTarget != nullptr) {
        delete sDefaultLogTarget;
        sDefaultLogTarget = nullptr;
    }
    if (sRootLogSource != nullptr) {
        delete sRootLogSource;
        sRootLogSource = nullptr;
    }
    sDefaultLogger = nullptr;
    sLogSourceMap.clear();

    termSchemaHandlers();
    termFieldSelectors();
}

bool ELogSystem::initialize(ELogErrorHandler* errorHandler /* = nullptr */) {
    setErrorHandler(errorHandler);
    return initGlobals();
}

// TODO: refactor init code
bool ELogSystem::initializeLogFile(const char* logFilePath, uint32_t bufferSize /* = 0 */,
                                   bool useLock /* = false */,
                                   ELogErrorHandler* errorHandler /* = nullptr */,
                                   ELogFlushPolicy* flushPolicy /* = nullptr */,
                                   ELogFilter* logFilter /* = nullptr */,
                                   ELogFormatter* logFormatter /* = nullptr */) {
    setErrorHandler(errorHandler);
    if (!initGlobals()) {
        return false;
    }
    if (bufferSize > 0) {
        if (!setBufferedLogFileTarget(logFilePath, bufferSize, useLock, flushPolicy)) {
            termGlobals();
            return false;
        }
    } else {
        if (setLogFileTarget(logFilePath, flushPolicy) == ELOG_INVALID_TARGET_ID) {
            termGlobals();
            return false;
        }
    }

    if (logFilter != nullptr) {
        setLogFilter(logFilter);
    }
    if (logFormatter != nullptr) {
        setLogFormatter(logFormatter);
    }
    return true;
}

bool ELogSystem::initializeSegmentedLogFile(const char* logPath, const char* logName,
                                            uint32_t segmentLimitMB,
                                            ELogErrorHandler* errorHandler /* = nullptr */,
                                            ELogFlushPolicy* flushPolicy /* = nullptr */,
                                            ELogFilter* logFilter /* = nullptr */,
                                            ELogFormatter* logFormatter /* = nullptr */) {
    setErrorHandler(errorHandler);
    if (!initGlobals()) {
        return false;
    }
    if (setSegmentedLogFileTarget(logPath, logName, segmentLimitMB, flushPolicy) ==
        ELOG_INVALID_TARGET_ID) {
        termGlobals();
        return false;
    }

    if (logFilter != nullptr) {
        setLogFilter(logFilter);
    }
    if (logFormatter != nullptr) {
        setLogFormatter(logFormatter);
    }
    return true;
}

void ELogSystem::terminate() { termGlobals(); }

void ELogSystem::setErrorHandler(ELogErrorHandler* errorHandler) { sErrorHandler = errorHandler; }

void ELogSystem::reportError(const char* errorMsgFmt, ...) {
    va_list ap;
    va_start(ap, errorMsgFmt);
    reportError(errorMsgFmt, ap);
    va_end(ap);
}

void ELogSystem::reportErrorV(const char* errorMsgFmt, va_list ap) {
    // compute error message length, this requires copying variadic argument pointer
    va_list apCopy;
    va_copy(apCopy, ap);
    uint32_t requiredBytes = (vsnprintf(nullptr, 0, errorMsgFmt, apCopy) + 1);

    // format error message
    char* errorMsg = (char*)malloc(requiredBytes);
    vsnprintf(errorMsg, requiredBytes, errorMsgFmt, ap);

    // report error
    ELogErrorHandler* errorHandler = sErrorHandler ? sErrorHandler : &sDefaultErrorHandler;
    errorHandler->onError(errorMsg);
    va_end(apCopy);
}

void ELogSystem::reportSysError(const char* sysCall, const char* errorMsgFmt, ...) {
    int errCode = errno;
    reportError("System call %s() failed: %d (%s)", sysCall, errCode, sysErrorToStr(errCode));

    va_list ap;
    va_start(ap, errorMsgFmt);
    reportError(errorMsgFmt, ap);
    va_end(ap);
}

void ELogSystem::reportSysErrorCode(const char* sysCall, int errCode, const char* errorMsgFmt,
                                    ...) {
    reportError("System call %s() failed: %d (%s)", sysCall, errCode, sysErrorToStr(errCode));

    va_list ap;
    va_start(ap, errorMsgFmt);
    reportError(errorMsgFmt, ap);
    va_end(ap);
}

bool ELogSystem::registerSchemaHandler(const char* schemaName, ELogSchemaHandler* schemaHandler) {
    if (sSchemaHandlerCount == ELOG_MAX_SCHEMA) {
        ELogSystem::reportError("Cannot initialize %s schema handler, out of space", schemaName);
        return false;
    }
    uint32_t id = sSchemaHandlerCount;
    if (!sSchemaHandlerMap.insert(ELogSchemaHandlerMap::value_type(schemaName, id)).second) {
        ELogSystem::reportError("Cannot initialize %s schema handler, duplicate name", schemaName);
        return false;
    }
    sSchemaHandlers[sSchemaHandlerCount++] = schemaHandler;
    return true;
}

ELogSchemaHandler* ELogSystem::getSchemaHandler(const char* schemaName) {
    ELogSchemaHandler* schemaHandler = nullptr;
    ELogSchemaHandlerMap::iterator itr = sSchemaHandlerMap.find(schemaName);
    if (itr != sSchemaHandlerMap.end()) {
        schemaHandler = sSchemaHandlers[itr->second];
    }
    return schemaHandler;
}

bool ELogSystem::parseLogLevel(const char* logLevelStr, ELogLevel& logLevel,
                               ELogSource::PropagateMode& propagateMode) {
    const char* ptr = nullptr;
    if (!elogLevelFromStr(logLevelStr, logLevel, &ptr)) {
        reportError("Invalid log level: %s", logLevelStr);
        return false;
    }

    // parse optional propagation sign if there is any
    propagateMode = ELogSource::PropagateMode::PM_NONE;
    uint32_t parseLen = ptr - logLevelStr;
    uint32_t len = strlen(logLevelStr);
    if (parseLen < len) {
        // there are more chars, only one is allowed
        if (parseLen + 1 != len) {
            reportError(
                "Invalid excess chars at global log level: %s (only one character is allowed: '*', "
                "'+' or '-')",
                logLevelStr);
            return false;
        } else if (*ptr == '*') {
            propagateMode = ELogSource::PropagateMode::PM_SET;
        } else if (*ptr == '-') {
            propagateMode = ELogSource::PropagateMode::PM_RESTRICT;
        } else if (*ptr == '+') {
            propagateMode = ELogSource::PropagateMode::PM_LOOSE;
        } else {
            reportError(
                "Invalid excess chars at global log level: %s (only one character is allowed: '*', "
                "'+' or '-')",
                logLevelStr);
            return false;
        }
    }

    return true;
}

bool ELogSystem::configureRateLimit(const std::string rateLimitCfg) {
    uint32_t maxMsgPerSec = 0;
    std::size_t pos = 0;
    try {
        maxMsgPerSec = std::stoul(rateLimitCfg, &pos);
    } catch (std::exception& e) {
        ELogSystem::reportError("Invalid log_rate_limit value %s: (%s)", rateLimitCfg.c_str(),
                                e.what());
        return false;
    }
    if (pos != rateLimitCfg.length()) {
        ELogSystem::reportError("Excess characters at log_rate_limit value: %s",
                                rateLimitCfg.c_str());
        return false;
    }
    return setRateLimit(maxMsgPerSec);
}

bool ELogSystem::configureLogTarget(const std::string& logTargetCfg) {
    // the following formats are currently supported as a URL-like string
    //
    // sys://stdout
    // sys://stderr
    // sys://syslog
    //
    // file://path
    // file://path?segment-size-mb=<segment-size-mb>
    //
    // optional parameters (each set is mutually exclusive with other sets)
    // defer (no value associated)
    // queue-batch-size=<batch-size>,queue-timeout-millis=<timeout-millis>
    // quantum-buffer-size=<buffer-size>
    //
    // future provision:
    // tcp://host:port
    // udp://host:port
    // db://db-name?conn-string=<conn-string>&insert-statement=<insert-statement>
    // msgq://message-broker-name?conn-string=<conn-string>&queue=<queue-name>&topic=<topic-name>
    ELogTargetSpec logTargetSpec;
    if (!parseLogTargetSpec(logTargetCfg, logTargetSpec)) {
        reportError("Invalid log target specification: %s", logTargetCfg.c_str());
        return false;
    }

    // check for registered schemas
    ELogSchemaHandlerMap::iterator itr = sSchemaHandlerMap.find(logTargetSpec.m_scheme);
    if (itr != sSchemaHandlerMap.end()) {
        ELogSchemaHandler* schemaHandler = sSchemaHandlers[itr->second];
        ELogTarget* logTarget = schemaHandler->loadTarget(logTargetCfg, logTargetSpec);
        if (logTarget == nullptr) {
            reportError("Failed to load target for schema %s: %s", logTargetSpec.m_scheme.c_str(),
                        logTargetCfg.c_str());
            return false;
        }

        // apply compound target
        bool errorOccurred = false;
        ELogTarget* compoundTarget =
            applyCompoundTarget(logTarget, logTargetCfg, logTargetSpec, errorOccurred);
        if (errorOccurred) {
            reportError("Failed to apply compound log target specification");
            delete logTarget;
            return false;
        }
        if (compoundTarget != nullptr) {
            logTarget = compoundTarget;
        }

        // apply target name if any
        applyTargetName(logTarget, logTargetSpec);

        // apply target log level if any
        if (!applyTargetLogLevel(logTarget, logTargetCfg, logTargetSpec)) {
            delete logTarget;
            return false;
        }

        // apply log format if any
        if (!applyTargetLogFormat(logTarget, logTargetCfg, logTargetSpec)) {
            delete logTarget;
            return false;
        }

        // apply flush policy if any
        if (!applyTargetFlushPolicy(logTarget, logTargetCfg, logTargetSpec)) {
            delete logTarget;
            return false;
        }

        // apply rate limiter if any
        if (!applyTargetRateLimiter(logTarget, logTargetCfg, logTargetSpec)) {
            delete logTarget;
            return false;
        }

        if (addLogTarget(logTarget) == ELOG_INVALID_TARGET_ID) {
            reportError("Failed to add log target for schema %s: %s",
                        logTargetSpec.m_scheme.c_str(), logTargetCfg.c_str());
            delete logTarget;
            return false;
        }
        return true;
    }

    reportError("Invalid log target specification, unrecognized schema %s: %s",
                logTargetSpec.m_scheme.c_str(), logTargetCfg.c_str());
    return false;
}

bool ELogSystem::parseLogTargetSpec(const std::string& logTargetCfg,
                                    ELogTargetSpec& logTargetSpec) {
    // find scheme separator
    std::string::size_type schemeSepPos = logTargetCfg.find(ELOG_SCHEMA_MARKER);
    if (schemeSepPos == std::string::npos) {
        reportError("Invalid log target specification, missing scheme separator \'%s\': %s",
                    ELOG_SCHEMA_MARKER, logTargetCfg.c_str());
        return false;
    }

    logTargetSpec.m_scheme = logTargetCfg.substr(0, schemeSepPos);

    // parse until first '?'
    std::string::size_type qmarkPos = logTargetCfg.find('?', schemeSepPos + ELOG_SCHEMA_LEN);
    if (qmarkPos == std::string::npos) {
        logTargetSpec.m_path = logTargetCfg.substr(schemeSepPos + ELOG_SCHEMA_LEN);
        logTargetSpec.m_port = 0;
        tryParsePathAsHostPort(logTargetCfg, logTargetSpec);
        return true;
    }

    logTargetSpec.m_path = logTargetCfg.substr(schemeSepPos + ELOG_SCHEMA_LEN,
                                               qmarkPos - schemeSepPos - ELOG_SCHEMA_LEN);
    tryParsePathAsHostPort(logTargetCfg, logTargetSpec);

    // parse properties, separated by ampersand
    std::string::size_type prevPos = qmarkPos + 1;
    std::string::size_type sepPos = logTargetCfg.find('&', prevPos);
    do {
        // get property
        std::string prop = (sepPos == std::string::npos)
                               ? logTargetCfg.substr(prevPos)
                               : logTargetCfg.substr(prevPos, sepPos - prevPos);

        // parse to key=value and add to props map (could be there is no value specified)
        std::string::size_type equalPos = prop.find('=');
        if (equalPos != std::string::npos) {
            std::string key = prop.substr(0, equalPos);
            std::string value = prop.substr(equalPos + 1);
            insertPropOverride(logTargetSpec.m_props, key, value);
        } else {
            insertPropOverride(logTargetSpec.m_props, prop, "");
        }

        // find next token separator
        if (sepPos != std::string::npos) {
            prevPos = sepPos + 1;
            sepPos = logTargetCfg.find('&', prevPos);
        } else {
            prevPos = sepPos;
        }
    } while (prevPos != std::string::npos);
    return true;
}

void ELogSystem::insertPropOverride(ELogPropertyMap& props, const std::string& key,
                                    const std::string& value) {
    std::pair<ELogPropertyMap::iterator, bool> itrRes =
        props.insert(ELogPropertyMap::value_type(key, value));
    if (!itrRes.second) {
        itrRes.first->second = value;
    }
}

void ELogSystem::applyTargetName(ELogTarget* logTarget, const ELogTargetSpec& logTargetSpec) {
    ELogPropertyMap::const_iterator itr = logTargetSpec.m_props.find("name");
    if (itr != logTargetSpec.m_props.end()) {
        logTarget->setName(itr->second.c_str());
    }
}

bool ELogSystem::applyTargetLogLevel(ELogTarget* logTarget, const std::string& logTargetCfg,
                                     const ELogTargetSpec& logTargetSpec) {
    ELogPropertyMap::const_iterator itr = logTargetSpec.m_props.find("log_level");
    if (itr != logTargetSpec.m_props.end()) {
        ELogLevel logLevel = ELEVEL_INFO;
        if (!elogLevelFromStr(itr->second.c_str(), logLevel)) {
            reportError("Invalid log level '%s' specified in log target: %s", itr->second.c_str(),
                        logTargetCfg.c_str());
            return false;
        }
        logTarget->setLogLevel(logLevel);
    }
    return true;
}

bool ELogSystem::applyTargetLogFormat(ELogTarget* logTarget, const std::string& logTargetCfg,
                                      const ELogTargetSpec& logTargetSpec) {
    ELogPropertyMap::const_iterator itr = logTargetSpec.m_props.find("log_format");
    if (itr != logTargetSpec.m_props.end()) {
        ELogFormatter* logFormatter = new (std::nothrow) ELogFormatter();
        if (!logFormatter->initialize(itr->second.c_str())) {
            reportError("Invalid log format '%s' specified in log target: %s", itr->second.c_str(),
                        logTargetCfg.c_str());
            delete logFormatter;
            return false;
        }
        logTarget->setLogFormatter(logFormatter);
    }
    return true;
}

bool ELogSystem::applyTargetFlushPolicy(ELogTarget* logTarget, const std::string& logTargetCfg,
                                        const ELogTargetSpec& logTargetSpec) {
    ELogPropertyMap::const_iterator itr = logTargetSpec.m_props.find("flush_policy");
    if (itr != logTargetSpec.m_props.end()) {
        ELogFlushPolicy* flushPolicy = nullptr;
        const std::string& flushPolicyCfg = itr->second;
        if (flushPolicyCfg.compare("immediate") == 0) {
            flushPolicy = new (std::nothrow) ELogImmediateFlushPolicy();
        } else if (flushPolicyCfg.compare("never") == 0) {
            flushPolicy = new (std::nothrow) ELogNeverFlushPolicy();
        } else if (flushPolicyCfg.compare("count") == 0) {
            ELogPropertyMap::const_iterator itr2 = logTargetSpec.m_props.find("flush-count");
            if (itr2 == logTargetSpec.m_props.end()) {
                reportError(
                    "Invalid flush policy configuration, missing expected flush-count property for "
                    "flush_policy=count: %s",
                    logTargetCfg.c_str());
                return false;
            }
            uint32_t logCountLimit = 0;
            if (!parseIntProp("flush-count", logTargetCfg, itr2->second, logCountLimit, true)) {
                reportError(
                    "Invalid flush policy configuration, flush-count property value '%s' is an "
                    "ill-formed integer: %s",
                    itr2->second.c_str(), logTargetCfg.c_str());
                return false;
            }
            flushPolicy = new (std::nothrow) ELogCountFlushPolicy(logCountLimit);
        } else if (flushPolicyCfg.compare("size") == 0) {
            ELogPropertyMap::const_iterator itr2 = logTargetSpec.m_props.find("flush-size-bytes");
            if (itr2 == logTargetSpec.m_props.end()) {
                reportError(
                    "Invalid flush policy configuration, missing expected flush-size-bytes "
                    "property for flush-policy=size: %s",
                    logTargetCfg.c_str());
                return false;
            }
            uint32_t logSizeLimitBytes = 0;
            if (!parseIntProp("flush-size-bytes", logTargetCfg, itr2->second, logSizeLimitBytes,
                              true)) {
                reportError(
                    "Invalid flush policy configuration, flush-size-bytes property value '%s' is "
                    "an ill-formed integer: %s",
                    itr2->second.c_str(), logTargetCfg.c_str());
                return false;
            }
            flushPolicy = new (std::nothrow) ELogSizeFlushPolicy(logSizeLimitBytes);
        } else if (flushPolicyCfg.compare("time") == 0) {
            ELogPropertyMap::const_iterator itr2 =
                logTargetSpec.m_props.find("flush-timeout-millis");
            if (itr2 == logTargetSpec.m_props.end()) {
                reportError(
                    "Invalid flush policy configuration, missing expected flush-timeout-millis "
                    "property for flush-policy=time: %s",
                    logTargetCfg.c_str());
                return false;
            }
            uint32_t logTimeLimitMillis = 0;
            if (!parseIntProp("flush-timeout-millis", logTargetCfg, itr2->second,
                              logTimeLimitMillis, true)) {
                reportError(
                    "Invalid flush policy configuration, flush-timeout-millis property value '%s' "
                    "is an ill-formed integer: %s",
                    itr2->second.c_str(), logTargetCfg.c_str());
                return false;
            }
            flushPolicy = new (std::nothrow) ELogTimedFlushPolicy(logTimeLimitMillis, logTarget);
        } else if (flushPolicyCfg.compare("none") != 0) {
            reportError("Unrecognized flush policy configuration %s: %s", flushPolicyCfg.c_str(),
                        logTargetCfg.c_str());
            return false;
        }
        logTarget->setFlushPolicy(flushPolicy);
    }
    return true;
}

bool ELogSystem::applyTargetRateLimiter(ELogTarget* logTarget, const std::string& logTargetCfg,
                                        const ELogTargetSpec& logTargetSpec) {
    ELogPropertyMap::const_iterator itr = logTargetSpec.m_props.find("rate-limit-msg-per-sec");
    if (itr != logTargetSpec.m_props.end()) {
        const std::string& rateLimitCfg = itr->second;
        uint32_t maxMsgPerSec = 0;
        if (!parseIntProp("rate-limit-msg-per-sec", logTargetCfg, rateLimitCfg, maxMsgPerSec,
                          true)) {
            reportError(
                "Invalid rate limit configuration, property value '%s' is an ill-formed integer: "
                "%s",
                rateLimitCfg.c_str(), logTargetCfg.c_str());
            return false;
        }
        ELogRateLimiter* rateLimiter = new (std::nothrow) ELogRateLimiter(maxMsgPerSec);
        logTarget->setLogFilter(rateLimiter);
    }
    return true;
}

ELogTarget* ELogSystem::applyCompoundTarget(ELogTarget* logTarget, const std::string& logTargetCfg,
                                            const ELogTargetSpec& logTargetSpec,
                                            bool& errorOccurred) {
    // there could be optional poperties: deferred,
    // queue-batch-size=<batch-size>,queue-timeout-millis=<timeout-millis>
    // quantum-buffer-size=<buffer-size>, quantum-congestion-policy=wait/discard
    errorOccurred = true;
    bool deferred = false;
    uint32_t queueBatchSize = 0;
    uint32_t queueTimeoutMillis = 0;
    uint32_t quantumBufferSize = 0;
    ELogQuantumTarget::CongestionPolicy congestionPolicy =
        ELogQuantumTarget::CongestionPolicy::CP_WAIT;
    for (const ELogProperty& prop : logTargetSpec.m_props) {
        // parse deferred property
        if (prop.first.compare("deferred") == 0) {
            if (queueBatchSize > 0 || queueTimeoutMillis > 0 || quantumBufferSize > 0) {
                reportError(
                    "Deferred log target cannot be specified with queued or quantum target: %s",
                    logTargetCfg.c_str());
                return nullptr;
            }
            if (deferred) {
                reportError("Deferred log target can be specified only once: %s",
                            logTargetCfg.c_str());
                return nullptr;
            }
            deferred = true;
        }

        // parse queue batch size property
        else if (prop.first.compare("queue-batch-size") == 0) {
            if (deferred || quantumBufferSize > 0) {
                reportError(
                    "Queued log target cannot be specified with deferred or quantum target: %s",
                    logTargetCfg.c_str());
                return nullptr;
            }
            if (queueBatchSize > 0) {
                reportError("Queue batch size can be specified only once: %s",
                            logTargetCfg.c_str());
                return nullptr;
            }
            if (!parseIntProp("queue-batch-size", logTargetCfg, prop.second, queueBatchSize)) {
                return nullptr;
            }
        }

        // parse queue timeout millis property
        else if (prop.first.compare("queue-timeout-millis") == 0) {
            if (deferred || quantumBufferSize > 0) {
                reportError(
                    "Queued log target cannot be specified with deferred or quantum target: %s",
                    logTargetCfg.c_str());
                return nullptr;
            }
            if (queueTimeoutMillis > 0) {
                reportError("Queue timeout millis can be specified only once: %s",
                            logTargetCfg.c_str());
                return nullptr;
            }
            if (!parseIntProp("queue-timeout-millis", logTargetCfg, prop.second,
                              queueTimeoutMillis)) {
                return nullptr;
            }
        }

        // parse quantum buffer size
        else if (prop.first.compare("quantum-buffer-size") == 0) {
            if (deferred || queueBatchSize > 0 || queueTimeoutMillis > 0) {
                reportError(
                    "Quantum log target cannot be specified with deferred or queued target: %s",
                    logTargetCfg.c_str());
                return nullptr;
            }
            if (quantumBufferSize > 0) {
                reportError("Quantum buffer size can be specified only once: %s",
                            logTargetCfg.c_str());
                return nullptr;
            }
            if (!parseIntProp("quantum-buffer-size", logTargetCfg, prop.second,
                              quantumBufferSize)) {
                return nullptr;
            }
        }

        // parse quantum buffer size
        else if (prop.first.compare("quantum-congestion-policy") == 0) {
            if (deferred || queueBatchSize > 0 || queueTimeoutMillis > 0) {
                reportError(
                    "Quantum log target cannot be specified with deferred or queued target: %s",
                    logTargetCfg.c_str());
                return nullptr;
            }
            if (prop.second.compare("wait") == 0) {
                congestionPolicy = ELogQuantumTarget::CongestionPolicy::CP_WAIT;
            } else if (prop.second.compare("discard-log") == 0) {
                congestionPolicy = ELogQuantumTarget::CongestionPolicy::CP_DISCARD_LOG;
            } else if (prop.second.compare("discard-all") == 0) {
                congestionPolicy = ELogQuantumTarget::CongestionPolicy::CP_DISCARD_ALL;
            } else {
                reportError("Invalid quantum log target congestion policy value '%s': %s",
                            prop.second.c_str(), logTargetCfg.c_str());
                return nullptr;
            }
        }
    }

    if (queueBatchSize > 0 && queueTimeoutMillis == 0) {
        reportError("Missing queue-timeout-millis parameter in log target specification: %s",
                    logTargetCfg.c_str());
        return nullptr;
    }

    if (queueBatchSize == 0 && queueTimeoutMillis > 0) {
        reportError("Missing queue-batch-size parameter in log target specification: %s",
                    logTargetCfg.c_str());
        return nullptr;
    }

    ELogTarget* compoundLogTarget = nullptr;
    if (deferred) {
        compoundLogTarget = new (std::nothrow) ELogDeferredTarget(logTarget);
    } else if (queueBatchSize > 0) {
        compoundLogTarget =
            new (std::nothrow) ELogQueuedTarget(logTarget, queueBatchSize, queueTimeoutMillis);
    } else if (quantumBufferSize > 0) {
        compoundLogTarget =
            new (std::nothrow) ELogQuantumTarget(logTarget, quantumBufferSize, congestionPolicy);
    }

    errorOccurred = false;
    return compoundLogTarget;
}

void ELogSystem::tryParsePathAsHostPort(const std::string& logTargetCfg,
                                        ELogTargetSpec& logTargetSpec) {
    std::string::size_type colonPos = logTargetSpec.m_path.find(':');
    if (colonPos != std::string::npos) {
        if (!parseIntProp("port", logTargetCfg, logTargetSpec.m_path.substr(colonPos + 1),
                          logTargetSpec.m_port, false)) {
            return;
        }
        logTargetSpec.m_host = logTargetSpec.m_path.substr(0, colonPos);
    }
}

bool ELogSystem::configureFromProperties(const ELogPropertySequence& props,
                                         bool defineLogSources /* = false */,
                                         bool defineMissingPath /* = false */) {
    // configure log format (unrelated to order of appearance)
    // NOTE: only one such item is expected
    std::string logFormatCfg;
    if (getProp(props, "log_format", logFormatCfg)) {
        if (!configureLogFormat(logFormatCfg.c_str())) {
            reportError("Invalid log format in properties: %s", logFormatCfg.c_str());
            return false;
        }
    }

    // configure global rate limit
    std::string rateLimitCfg;
    if (getProp(props, "log_rate_limit", rateLimitCfg)) {
        if (!configureRateLimit(rateLimitCfg)) {
            return false;
        }
    }

    std::vector<ELogLevelCfg> logLevelCfg;
    ELogLevel logLevel = ELEVEL_INFO;
    ELogSource::PropagateMode propagateMode = ELogSource::PropagateMode::PM_NONE;

    const char* suffix = ".log_level";
    uint32_t suffixLen = strlen(suffix);

    for (const ELogProperty& prop : props) {
        // check if this is root log level
        if (prop.first.compare("log_level") == 0) {
            // global log level
            if (!parseLogLevel(prop.second.c_str(), logLevel, propagateMode)) {
                reportError("Invalid global log level: %s", prop.second.c_str());
                return false;
            }
            logLevelCfg.push_back({getRootLogSource(), logLevel, propagateMode});
            continue;
        }

        // check for log target
        if (prop.first.compare("log_target") == 0) {
            // configure log target
            if (!configureLogTarget(prop.second)) {
                return false;
            }
            continue;
        }

        // configure log levels of log sources
        // search for ".log_level" with a dot, in order to filter out global log_level key
        // NOTE: when definining log sources, we must first define all log sources, then set log
        // level to configured level and apply log level propagation. If we apply propagation
        // before child log sources are defined, then propagation is lost.
        if (prop.first.ends_with(suffix)) {
            const std::string& key = prop.first;
            // shave off trailing ".log_level" (that includes last dot)
            std::string sourceName = key.substr(0, key.size() - suffixLen);
            ELogSource* logSource = defineLogSources
                                        ? defineLogSource(sourceName.c_str(), defineMissingPath)
                                        : getLogSource(sourceName.c_str());
            if (logSource == nullptr) {
                reportError("Invalid log source name: %s", sourceName.c_str());
                return false;
            }
            ELogSource::PropagateMode propagateMode = ELogSource::PropagateMode::PM_NONE;
            if (!parseLogLevel(prop.second.c_str(), logLevel, propagateMode)) {
                reportError("Invalid source &s log level: %s", sourceName.c_str(),
                            prop.second.c_str());
                return false;
            }
            logLevelCfg.push_back({logSource, logLevel, propagateMode});
        }
    }

    // now we can apply log level propagation
    for (const ELogLevelCfg& cfg : logLevelCfg) {
        ELOG_DEBUG("Setting %s log level to %s (propagate - %u)",
                   cfg.m_logSource->getQualifiedName(), elogLevelToStr(cfg.m_logLevel),
                   (uint32_t)cfg.m_propagationMode);
        cfg.m_logSource->setLogLevel(cfg.m_logLevel, cfg.m_propagationMode);
    }

    return true;
}

ELogTargetId ELogSystem::setLogTarget(ELogTarget* logTarget, bool printBanner /* = false */) {
    // first start the log target
    if (!logTarget->start()) {
        reportError("Failed to start log target");
        return ELOG_INVALID_TARGET_ID;
    }

    // check if this is the first log target or not
    if (!sLogTargets.empty()) {
        for (ELogTarget* logTarget : sLogTargets) {
            if (logTarget != nullptr) {
                logTarget->stop();
            }
        }
        sLogTargets.clear();
    }

    sLogTargets.push_back(logTarget);
    if (printBanner) {
        ELOG_INFO("======================================================");
    }
    return (ELogTargetId)(sLogTargets.size() - 1);
}

ELogTargetId ELogSystem::setLogFileTarget(const char* logFilePath,
                                          ELogFlushPolicy* flushPolicy /* = nullptr */,
                                          bool printBanner /* = true */) {
    // create new log target
    ELogFileTarget* logTarget = new (std::nothrow) ELogFileTarget(logFilePath, flushPolicy);
    if (logTarget == nullptr) {
        reportError("Failed to create log file target, out of memory");
        return ELOG_INVALID_TARGET_ID;
    }

    ELogTargetId logTargetId = setLogTarget(logTarget, printBanner);
    if (logTargetId == ELOG_INVALID_TARGET_ID) {
        delete logTarget;
    }
    return logTargetId;
}

ELogTargetId ELogSystem::setLogFileTarget(FILE* fileHandle,
                                          ELogFlushPolicy* flushPolicy /* = nullptr */,
                                          bool printBanner /* = false */) {
    // create new log target
    ELogFileTarget* logTarget = new (std::nothrow) ELogFileTarget(fileHandle, flushPolicy);
    if (logTarget == nullptr) {
        reportError("Failed to create log target, out of memory");
        return ELOG_INVALID_TARGET_ID;
    }

    ELogTargetId logTargetId = setLogTarget(logTarget, printBanner);
    if (logTargetId == ELOG_INVALID_TARGET_ID) {
        delete logTarget;
    }
    return logTargetId;
}

ELogTargetId ELogSystem::setBufferedLogFileTarget(const char* logFilePath, uint32_t bufferSize,
                                                  bool useLock /* = true */,
                                                  ELogFlushPolicy* flushPolicy /* = nullptr */,
                                                  bool printBanner /* = false */) {
    // verify parameters
    if (bufferSize == 0) {
        reportError("Invalid zero buffer size for buffered file log target");
        return ELOG_INVALID_TARGET_ID;
    }

    // create new log target
    ELogTarget* logTarget =
        new (std::nothrow) ELogBufferedFileTarget(logFilePath, bufferSize, useLock, flushPolicy);
    if (logTarget == nullptr) {
        reportError("Failed to create log file target, out of memory");
        return ELOG_INVALID_TARGET_ID;
    }

    ELogTargetId logTargetId = setLogTarget(logTarget, printBanner);
    if (logTargetId == ELOG_INVALID_TARGET_ID) {
        delete logTarget;
    }
    return logTargetId;
}

ELogTargetId ELogSystem::setBufferedLogFileTarget(FILE* fileHandle, uint32_t bufferSize,
                                                  bool useLock /* = true */,
                                                  ELogFlushPolicy* flushPolicy /* = nullptr */,
                                                  bool printBanner /* = false */) {
    // verify parameters
    if (bufferSize == 0) {
        reportError("Invalid zero buffer size for buffered file log target");
        return ELOG_INVALID_TARGET_ID;
    }

    // create new log target
    ELogTarget* logTarget =
        new (std::nothrow) ELogBufferedFileTarget(fileHandle, bufferSize, useLock, flushPolicy);
    if (logTarget == nullptr) {
        reportError("Failed to create log file target, out of memory");
        return ELOG_INVALID_TARGET_ID;
    }

    ELogTargetId logTargetId = setLogTarget(logTarget, printBanner);
    if (logTargetId == ELOG_INVALID_TARGET_ID) {
        delete logTarget;
    }
    return logTargetId;
}

ELogTargetId ELogSystem::setSegmentedLogFileTarget(const char* logPath, const char* logName,
                                                   uint32_t segmentLimitMB,
                                                   ELogFlushPolicy* flushPolicy /* = nullptr */,
                                                   bool printBanner /* = true */) {
    // create new log target
    ELogTarget* logTarget =
        new (std::nothrow) ELogSegmentedFileTarget(logPath, logName, segmentLimitMB, flushPolicy);
    if (logTarget == nullptr) {
        reportError("Failed to create segmented log file target, out of memory");
        return ELOG_INVALID_TARGET_ID;
    }

    ELogTargetId logTargetId = setLogTarget(logTarget, printBanner);
    if (logTargetId == ELOG_INVALID_TARGET_ID) {
        delete logTarget;
    }
    return logTargetId;
}

ELogTargetId ELogSystem::addLogTarget(ELogTarget* logTarget) {
    if (!logTarget->start()) {
        return ELOG_INVALID_TARGET_ID;
    }

    // find vacant slot ro add a new one
    for (uint32_t i = 0; i < sLogTargets.size(); ++i) {
        if (sLogTargets[i] == nullptr) {
            sLogTargets[i] = logTarget;
            return i;
        }
    }

    // otherwise add a new slot
    sLogTargets.push_back(logTarget);
    return (ELogTargetId)(sLogTargets.size() - 1);
}

ELogTargetId ELogSystem::addLogFileTarget(const char* logFilePath, uint32_t bufferSize /* = 0 */,
                                          bool useLock /* = false */,
                                          ELogFlushPolicy* flushPolicy /* = nullptr */) {
    // create new log target
    ELogTarget* logTarget = nullptr;
    if (bufferSize > 0) {
        logTarget = new (std::nothrow)
            ELogBufferedFileTarget(logFilePath, bufferSize, useLock, flushPolicy);
    } else {
        logTarget = new (std::nothrow) ELogFileTarget(logFilePath, flushPolicy);
    }
    if (logTarget == nullptr) {
        reportError("Failed to create log target, out of memory");
        return ELOG_INVALID_TARGET_ID;
    }

    ELogTargetId logTargetId = addLogTarget(logTarget);
    if (logTargetId == ELOG_INVALID_TARGET_ID) {
        delete logTarget;
    }
    return logTargetId;
}

ELogTargetId ELogSystem::addLogFileTarget(FILE* fileHandle, uint32_t bufferSize /* = 0 */,
                                          bool useLock /* = false */,
                                          ELogFlushPolicy* flushPolicy /* = nullptr */) {
    ELogTarget* logTarget = nullptr;
    if (bufferSize > 0) {
        logTarget =
            new (std::nothrow) ELogBufferedFileTarget(fileHandle, bufferSize, useLock, flushPolicy);
    } else {
        logTarget = new (std::nothrow) ELogFileTarget(fileHandle, flushPolicy);
    }
    if (logTarget == nullptr) {
        reportError("Failed to create log target, out of memory");
        return ELOG_INVALID_TARGET_ID;
    }

    ELogTargetId logTargetId = addLogTarget(logTarget);
    if (logTargetId == ELOG_INVALID_TARGET_ID) {
        delete logTarget;
    }
    return logTargetId;
}

ELogTargetId ELogSystem::addSegmentedLogFileTarget(const char* logPath, const char* logName,
                                                   uint32_t segmentLimitMB,
                                                   ELogFlushPolicy* flushPolicy /* = nullptr */) {
    // create new log target
    ELogTarget* logTarget =
        new (std::nothrow) ELogSegmentedFileTarget(logPath, logName, segmentLimitMB, flushPolicy);
    if (logTarget == nullptr) {
        reportError("Failed to create segmented log file target, out of memory");
        return ELOG_INVALID_TARGET_ID;
    }

    ELogTargetId logTargetId = addLogTarget(logTarget);
    if (logTargetId == ELOG_INVALID_TARGET_ID) {
        delete logTarget;
    }
    return logTargetId;
}

ELogTargetId ELogSystem::addStdErrLogTarget() { return addLogFileTarget(stderr); }

ELogTargetId ELogSystem::addStdOutLogTarget() { return addLogFileTarget(stdout); }

ELogTargetId ELogSystem::addSysLogTarget() {
#ifdef ELOG_LINUX
    ELogSysLogTarget* logTarget = new (std::nothrow) ELogSysLogTarget();
    return addLogTarget(logTarget);
#else
    return ELOG_INVALID_TARGET_ID;
#endif
}

ELogTarget* ELogSystem::getLogTarget(ELogTargetId targetId) {
    if (targetId >= sLogTargets.size()) {
        return nullptr;
    }
    return sLogTargets[targetId];
}

ELogTarget* ELogSystem::getLogTarget(const char* logTargetName) {
    for (ELogTarget* logTarget : sLogTargets) {
        if (strcmp(logTarget->getName(), logTargetName) == 0) {
            return logTarget;
        }
    }
    return nullptr;
}

void ELogSystem::removeLogTarget(ELogTargetId targetId) {
    // be careful, if this is the last log target, we must put back stderr
    if (targetId >= sLogTargets.size()) {
        // silently ignore invalid request
        return;
    }

    if (sLogTargets[targetId] == nullptr) {
        // silently ignore repeated request
        return;
    }

    // delete the log target and put null
    // we cannot shrink the vector because that will change log target indices
    sLogTargets[targetId]->stop();
    delete sLogTargets[targetId];
    sLogTargets[targetId] = nullptr;

    // find largest suffix of removed log targets
    int lastLogTarget = -1;
    for (int i = sLogTargets.size() - 1; i >= 0; --i) {
        if (sLogTargets[targetId] != nullptr) {
            // at least one log target active, so we can return
            lastLogTarget = i;
            break;
        }
    }

    // remove unused suffix
    sLogTargets.resize(lastLogTarget + 1);
}

void ELogSystem::removeLogTarget(ELogTarget* target) {
    // find log target and remove it
    for (uint32_t i = 0; i < sLogTargets.size(); ++i) {
        if (sLogTargets[i] == target) {
            removeLogTarget(i);
        }
    }
}

static void parseSourceName(const std::string& qualifiedName, std::vector<std::string>& namePath) {
    std::string::size_type prevDotPos = 0;
    std::string::size_type dotPos = qualifiedName.find('.');
    while (dotPos != std::string::npos) {
        if (dotPos > prevDotPos) {
            namePath.push_back(qualifiedName.substr(prevDotPos, dotPos - prevDotPos));
        }
        prevDotPos = dotPos + 1;
        dotPos = qualifiedName.find('.', prevDotPos);
    }
    if (prevDotPos < qualifiedName.length()) {
        namePath.push_back(qualifiedName.substr(prevDotPos));
    }
}

ELogSource* ELogSystem::addChildSource(ELogSource* parent, const char* sourceName) {
    ELogSource* logSource = new (std::nothrow) ELogSource(allocLogSourceId(), sourceName, parent);
    if (!parent->addChild(logSource)) {
        // impossible
        // TODO: consider having an error listener to pass to user all errors and let the user deal
        // with them, dumping to stderr is not acceptable in an infrastructure library, but there is
        // also no sense in defining error codes, convert to string, etc.
        delete logSource;
        return nullptr;
    }

    bool res =
        sLogSourceMap.insert(ELogSourceMap::value_type(logSource->getId(), logSource)).second;
    if (!res) {
        // internal error, roll back
        parent->removeChild(logSource->getName());
        delete logSource;
        return nullptr;
    }
    return logSource;
}

// log sources
ELogSource* ELogSystem::defineLogSource(const char* qualifiedName,
                                        bool defineMissingPath /* = false */) {
    std::unique_lock<std::mutex> lock(sSourceTreeLock);
    // parse name to components and start traveling up to last component
    std::vector<std::string> namePath;
    parseSourceName(qualifiedName, namePath);

    ELogSource* currSource = sRootLogSource;
    uint32_t namecount = namePath.size();
    for (uint32_t i = 0; i < namecount - 1; ++i) {
        ELogSource* childSource = currSource->getChild(namePath[i].c_str());
        if (childSource == nullptr && defineMissingPath) {
            childSource = addChildSource(currSource, namePath[i].c_str());
        }
        if (childSource == nullptr) {
            // TODO: partial failures are left as dangling sources, is that ok?
            return nullptr;
        }
        currSource = childSource;
    }

    // make sure name does not exist already
    const char* logSourceName = namePath.back().c_str();
    ELogSource* logSource = currSource->getChild(logSourceName);
    if (logSource != nullptr) {
        return logSource;
    }

    // otherwise create it and add it
    logSource = addChildSource(currSource, logSourceName);

    // in case of a new log source, we check if there is an environment variable for configuring its
    // log level. The expected format is: <qualified-log-source-name>_log_level = <elog-level>
    // every dot in the qualified name is replaced with underscore
    std::string envVarName = std::string(qualifiedName) + "_log_level";
    std::replace(envVarName.begin(), envVarName.end(), '.', '_');
    char* envVarValue = getenv(envVarName.c_str());
    if (envVarValue != nullptr) {
        ELogLevel logLevel = ELEVEL_INFO;
        if (elogLevelFromStr(envVarValue, logLevel)) {
            logSource->setLogLevel(logLevel, ELogSource::PropagateMode::PM_NONE);
        }
    }

    return logSource;
}

ELogSource* ELogSystem::getLogSource(const char* qualifiedName) {
    std::unique_lock<std::mutex> lock(sSourceTreeLock);
    std::vector<std::string> namePath;
    parseSourceName(qualifiedName, namePath);
    ELogSource* currSource = sRootLogSource;
    uint32_t namecount = namePath.size();
    for (uint32_t i = 0; i < namecount; ++i) {
        currSource = currSource->getChild(namePath[i].c_str());
        if (currSource == nullptr) {
            break;
        }
    }
    return currSource;
}

ELogSource* ELogSystem::getLogSource(ELogSourceId logSourceId) {
    ELogSource* logSource = nullptr;
    std::unique_lock<std::mutex> lock(sSourceTreeLock);
    ELogSourceMap::iterator itr = sLogSourceMap.find(logSourceId);
    if (itr != sLogSourceMap.end()) {
        logSource = itr->second;
    }
    return logSource;
}

ELogSource* ELogSystem::getRootLogSource() { return sRootLogSource; }

// void configureLogSourceLevel(const char* qualifiedName, ELogLevel logLevel);

// logger interface
ELogLogger* ELogSystem::getDefaultLogger() { return sDefaultLogger; }

ELogLogger* ELogSystem::getSharedLogger(const char* qualifiedSourceName) {
    ELogLogger* logger = nullptr;
    ELogSource* source = getLogSource(qualifiedSourceName);
    if (source != nullptr) {
        logger = source->createSharedLogger();
    }
    return logger;
}

ELogLogger* ELogSystem::getPrivateLogger(const char* qualifiedSourceName) {
    ELogLogger* logger = nullptr;
    ELogSource* source = getLogSource(qualifiedSourceName);
    if (source != nullptr) {
        logger = source->createPrivateLogger();
    }
    return logger;
}

// ELogLogger* ELogSystem::getMultiThreadedLogger(const char* sourceName);
// ELogLogger* ELogSystem::getSingleThreadedLogger(const char* sourceName);

// log formatting
bool ELogSystem::configureLogFormat(const char* logFormat) {
    ELogFormatter* logFormatter = new (std::nothrow) ELogFormatter();
    if (!logFormatter->initialize(logFormat)) {
        delete logFormatter;
        return false;
    }
    setLogFormatter(logFormatter);
    return true;
}

void ELogSystem::setLogFormatter(ELogFormatter* logFormatter) {
    if (sGlobalFormatter != nullptr) {
        delete sGlobalFormatter;
    }
    sGlobalFormatter = logFormatter;
}

void ELogSystem::formatLogMsg(const ELogRecord& logRecord, std::string& logMsg) {
    sGlobalFormatter->formatLogMsg(logRecord, logMsg);
}

void ELogSystem::setCurrentThreadName(const char* threadName) {
    setCurrentThreadNameField(threadName);
}

// global log filtering
void ELogSystem::setLogFilter(ELogFilter* logFilter) {
    if (sGlobalFilter != nullptr) {
        delete sGlobalFilter;
    }
    sGlobalFilter = logFilter;
}

bool ELogSystem::setRateLimit(uint32_t maxMsgPerSecond) {
    ELogRateLimiter* rateLimiter = new (std::nothrow) ELogRateLimiter(maxMsgPerSecond);
    if (rateLimiter == nullptr) {
        reportError("Failed to set rate limit, out of memory");
        return false;
    }
    setLogFilter(rateLimiter);
    return true;
}

bool ELogSystem::filterLogMsg(const ELogRecord& logRecord) {
    bool res = true;
    if (sGlobalFilter != nullptr) {
        res = sGlobalFilter->filterLogRecord(logRecord);
    }
    return res;
}

void ELogSystem::log(const ELogRecord& logRecord) {
    bool logged = false;
    for (ELogTarget* logTarget : sLogTargets) {
        logTarget->log(logRecord);
        logged = true;
    }

    // by default, if no log target is defined yet, log is redirected to stderr
    if (!logged) {
        sDefaultLogTarget->log(logRecord);
    }
}

char* ELogSystem::sysErrorToStr(int sysErrorCode) {
#ifdef ELOG_WINDOWS
    return strerror(sysErrorCode);
#else
    static thread_local char buf[256];
    return strerror_r(sysErrorCode, buf, 256);
#endif
}

#ifdef ELOG_WINDOWS
char* ELogSystem::win32SysErrorToStr(unsigned long sysErrorCode) {
    LPSTR messageBuffer = nullptr;
    size_t size = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, sysErrorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0,
        NULL);
    return messageBuffer;
}

void ELogSystem::win32FreeErrorStr(char* errStr) { LocalFree(errStr); }
#endif

}  // namespace elog
