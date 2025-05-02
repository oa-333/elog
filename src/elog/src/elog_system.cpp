#include "elog_system.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <unordered_map>

#include "elog_file_target.h"
#include "elog_flush_policy.h"
#include "elog_formatter.h"
#include "elog_segmented_file_target.h"

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
static ELogImmediateFlushPolicy sDefaultPolicy;
static ELogFormatter* sGlobalFormatter = nullptr;
static ELogFlushPolicy* sFlushPolicy = nullptr;

bool ELogSystem::initGlobals() {
    // root logger has no name
    // NOTE: this is the only place where we cannot use logging macros
    sRootLogSource = new (std::nothrow) ELogSource(allocLogSourceId(), "");
    if (sRootLogSource == nullptr) {
        fprintf(stderr, "Failed to create root log source, out of memory\n");
        return false;
    }

    // add to global map
    bool res =
        sLogSourceMap.insert(ELogSourceMap::value_type(sRootLogSource->getId(), sRootLogSource))
            .second;
    if (!res) {
        fprintf(stderr,
                "Failed to insert root log source to global source map (duplicate found)\n");
        termGlobals();
        return false;
    }

    sDefaultLogger = sRootLogSource->createSharedLogger();
    if (sDefaultLogger == nullptr) {
        fprintf(stderr, "Failed to create default logger, out of memory\n");
        termGlobals();
        return false;
    }
    sGlobalFormatter = new (std::nothrow) ELogFormatter();
    if (!sGlobalFormatter->initialize()) {
        fprintf(stderr, "Failed to create default logger, out of memory\n");
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

    if (sGlobalFormatter != nullptr) {
        delete sGlobalFormatter;
        sGlobalFormatter = nullptr;
    }
    if (sRootLogSource != nullptr) {
        delete sRootLogSource;
        sRootLogSource = nullptr;
    }
    sDefaultLogger = nullptr;
    sLogSourceMap.clear();
}

bool ELogSystem::initialize() {
    if (!initGlobals()) {
        return false;
    }
    if (setLogFileTarget(stderr) == ELOG_INVALID_TARGET_ID) {
        termGlobals();
        return false;
    }
    initFieldSelectors();
    return true;
}

// TODO: refactor init code
bool ELogSystem::initializeLogFile(const char* logFilePath,
                                   ELogFlushPolicy* flushPolicy /* = nullptr */,
                                   ELogFilter* logFilter /* = nullptr */,
                                   ELogFormatter* logFormatter /* = nullptr */) {
    if (!initGlobals()) {
        return false;
    }
    if (setLogFileTarget(logFilePath, flushPolicy) == ELOG_INVALID_TARGET_ID) {
        termGlobals();
        return false;
    }

    if (logFilter != nullptr) {
        setLogFilter(logFilter);
    }
    if (logFormatter != nullptr) {
        setLogFormatter(logFormatter);
    }
    initFieldSelectors();
    return true;
}

bool ELogSystem::initializeSegmentedLogFile(const char* logPath, const char* logName,
                                            uint32_t segmentLimitMB,
                                            ELogFlushPolicy* flushPolicy /* = nullptr */,
                                            ELogFilter* logFilter /* = nullptr */,
                                            ELogFormatter* logFormatter /* = nullptr */) {
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
    initFieldSelectors();
    return true;
}

void ELogSystem::terminate() {
    for (ELogTarget* logTarget : sLogTargets) {
        if (logTarget != nullptr) {
            logTarget->stop();
            delete logTarget;
        }
    }
    sLogTargets.clear();

    setLogFormatter(nullptr);
    setLogFilter(nullptr);
    termGlobals();
}

bool ELogSystem::parseLogLevel(const char* logLevelStr, ELogLevel& logLevel,
                               ELogSource::PropagateMode& propagateMode) {
    const char* ptr = nullptr;
    if (!elogLevelFromStr(logLevelStr, logLevel, &ptr)) {
        ELOG_ERROR("Invalid log level: %s", logLevelStr);
        return false;
    }

    // parse optional propagation sign if there is any
    propagateMode = ELogSource::PropagateMode::PM_NONE;
    uint32_t parseLen = ptr - logLevelStr;
    uint32_t len = strlen(logLevelStr);
    if (parseLen < len) {
        // there are more chars, only one is allowed
        if (parseLen + 1 != len) {
            ELOG_ERROR(
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
            ELOG_ERROR(
                "Invalid excess chars at global log level: %s (only one character is allowed: '*', "
                "'+' or '-')",
                logLevelStr);
            return false;
        }
    }

    return true;
}

bool ELogSystem::configureFromProperties(const ELogPropertyMap& props,
                                         bool defineLogSources /* = false */,
                                         bool defineMissingPath /* = false */) {
    // configure log format (unrelated to order of appearance)
    ELogPropertyMap::const_iterator itr = std::find_if(
        props.begin(), props.end(),
        [](const ELogProperty& prop) { return prop.first.compare("log_format") == 0; });
    if (itr != props.end()) {
        if (!configureLogFormat(itr->second.c_str())) {
            ELOG_ERROR("Invalid log format in properties: %s", itr->second.c_str());
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
                ELOG_ERROR("Invalid global log level: %s", prop.second.c_str());
                return false;
            }
            logLevelCfg.push_back({getRootLogSource(), logLevel, propagateMode});
        }

        // configure log levels of log sources
        // search for ".log_level" with a dot, in order to filter out global log_level key
        // NOTE: when definining log sources, we must first define all log sources, then set log
        // level to configured level and apply log level propagation. If we apply propagation before
        // child log sources are defined, then propagation is lost.
        if (prop.first.ends_with(suffix)) {
            const std::string& key = prop.first;
            // shave off trailing ".log_level" (that includes last dot)
            std::string sourceName = key.substr(0, key.size() - suffixLen);
            ELogSource* logSource = defineLogSources
                                        ? defineLogSource(sourceName.c_str(), defineMissingPath)
                                        : getLogSource(sourceName.c_str());
            if (logSource == nullptr) {
                ELOG_ERROR("Invalid log source name: %s", sourceName.c_str());
                return false;
            }
            ELogSource::PropagateMode propagateMode = ELogSource::PropagateMode::PM_NONE;
            if (!parseLogLevel(prop.second.c_str(), logLevel, propagateMode)) {
                ELOG_ERROR("Invalid source &s log level: %s", sourceName.c_str(),
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
        ELOG_ERROR("Failed to start log target");
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
    if (flushPolicy == nullptr) {
        flushPolicy = &sDefaultPolicy;
    }
    ELogFileTarget* logTarget = new (std::nothrow) ELogFileTarget(logFilePath, flushPolicy);
    if (logTarget == nullptr) {
        ELOG_ERROR("Failed to create log file target, out of memory");
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
    if (flushPolicy == nullptr) {
        flushPolicy = &sDefaultPolicy;
    }
    ELogFileTarget* logTarget = new (std::nothrow) ELogFileTarget(fileHandle, flushPolicy);
    if (logTarget == nullptr) {
        ELOG_ERROR("Failed to create log target, out of memory");
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
    if (flushPolicy == nullptr) {
        flushPolicy = &sDefaultPolicy;
    }
    ELogTarget* logTarget =
        new (std::nothrow) ELogSegmentedFileTarget(logPath, logName, segmentLimitMB, flushPolicy);
    if (logTarget == nullptr) {
        ELOG_ERROR("Failed to create segmented log file target, out of memory");
        return ELOG_INVALID_TARGET_ID;
    }

    ELogTargetId logTargetId = setLogTarget(logTarget, printBanner);
    if (logTargetId == ELOG_INVALID_TARGET_ID) {
        delete logTarget;
    }
    return logTargetId;
}

ELogTargetId ELogSystem::addLogTarget(ELogTarget* logTarget) {
    if (!sLogTargets[0]->start()) {
        return ELOG_INVALID_TARGET_ID;
    }
    sLogTargets.push_back(logTarget);
    return (ELogTargetId)(sLogTargets.size() - 1);
}

ELogTargetId ELogSystem::addLogFileTarget(const char* logFilePath,
                                          ELogFlushPolicy* flushPolicy /* = nullptr */) {
    // create new log target
    if (flushPolicy == nullptr) {
        flushPolicy = &sDefaultPolicy;
    }
    ELogFileTarget* logTarget = new (std::nothrow) ELogFileTarget(logFilePath, flushPolicy);
    if (logTarget == nullptr) {
        ELOG_ERROR("Failed to create log target, out of memory");
        return ELOG_INVALID_TARGET_ID;
    }

    ELogTargetId logTargetId = addLogTarget(logTarget);
    if (logTargetId == ELOG_INVALID_TARGET_ID) {
        delete logTarget;
    }
    return logTargetId;
}

ELogTargetId ELogSystem::addLogFileTarget(FILE* fileHandle,
                                          ELogFlushPolicy* flushPolicy /* = nullptr */) {
    if (flushPolicy == nullptr) {
        flushPolicy = &sDefaultPolicy;
    }
    ELogFileTarget* logTarget = new (std::nothrow) ELogFileTarget(fileHandle, flushPolicy);
    if (logTarget == nullptr) {
        ELOG_ERROR("Failed to create log target, out of memory");
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
    if (flushPolicy == nullptr) {
        flushPolicy = &sDefaultPolicy;
    }
    ELogTarget* logTarget =
        new (std::nothrow) ELogSegmentedFileTarget(logPath, logName, segmentLimitMB, flushPolicy);
    if (logTarget == nullptr) {
        ELOG_ERROR("Failed to create segmented log file target, out of memory");
        return ELOG_INVALID_TARGET_ID;
    }

    ELogTargetId logTargetId = addLogTarget(logTarget);
    if (logTargetId == ELOG_INVALID_TARGET_ID) {
        delete logTarget;
    }
    return logTargetId;
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

    // now check if all log sinks are removed
    for (uint32_t i = 0; i < sLogTargets.size(); ++i) {
        if (sLogTargets[targetId] != nullptr) {
            // at least one log target active, so we can return
            return;
        }
    }

    // no log sinks left, so put back stderr
    sLogTargets.clear();
    addLogFileTarget(stderr);
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
    namePath.push_back(qualifiedName.substr(prevDotPos));
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
//  void setLogFormatter(ELogFormatter* formatter);
void ELogSystem::formatLogMsg(const ELogRecord& logRecord, std::string& logMsg) {
    sGlobalFormatter->formatLogMsg(logRecord, logMsg);
}

// global log filtering
void ELogSystem::setLogFilter(ELogFilter* logFilter) {
    if (sGlobalFilter != nullptr) {
        delete sGlobalFilter;
    }
    sGlobalFilter = logFilter;
}

bool ELogSystem::filterLogMsg(const ELogRecord& logRecord) {
    bool res = true;
    if (sGlobalFilter != nullptr) {
        res = sGlobalFilter->filterLogRecord(logRecord);
    }
    return res;
}

void ELogSystem::log(const ELogRecord& logRecord) {
    for (ELogTarget* logTarget : sLogTargets) {
        logTarget->log(logRecord);
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
