#include "elog_system.h"

#include <atomic>
#include <cstring>
#include <mutex>
#include <unordered_map>

#include "elog_file_target.h"
#include "elog_flush_policy.h"
#include "elog_formatter.h"
#include "elog_segmented_file_target.h"

#ifdef __WIN32__
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace elog {

static ELogFilter* sGlobalFilter = nullptr;
static std::vector<ELogTarget*> sLogTargets;
static std::atomic<ELogSourceId> sNextLogSourceId;

static ELogSourceId allocLogSourceId() {
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
    sDefaultLogger = sRootLogSource->createLogger();
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
    if (sGlobalFormatter != nullptr) {
        delete sGlobalFormatter;
        sGlobalFormatter = nullptr;
    }
    if (sDefaultLogger != nullptr) {
        delete sDefaultLogger;
        sDefaultLogger = nullptr;
    }
    if (sRootLogSource != nullptr) {
        delete sRootLogSource;
        sRootLogSource = nullptr;
    }
}

bool ELogSystem::initialize() {
    if (!initGlobals()) {
        return false;
    }
    if (setFileLogTarget(stderr) == ELOG_INVALID_TARGET_ID) {
        termGlobals();
        return false;
    }
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

    ELogTarget* logTarget = new (std::nothrow) ELogFileTarget(logFilePath, flushPolicy);
    if (logTarget == nullptr) {
        ELOG_ERROR(getDefaultLogger(), "Failed to create log file target, out of memory");
        termGlobals();
        return ELOG_INVALID_TARGET_ID;
    }

    if (setLogTarget(logTarget) == ELOG_INVALID_TARGET_ID) {
        logTarget->stop();
        delete logTarget;
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

bool ELogSystem::initializeSegmentedLogFile(const char* logPath, const char* logName,
                                            uint32_t segmentLimitMB,
                                            ELogFlushPolicy* flushPolicy /* = nullptr */,
                                            ELogFilter* logFilter /* = nullptr */,
                                            ELogFormatter* logFormatter /* = nullptr */) {
    if (!initGlobals()) {
        return false;
    }

    ELogTarget* logTarget =
        new (std::nothrow) ELogSegmentedFileTarget(logPath, logName, segmentLimitMB, flushPolicy);
    if (logTarget == nullptr) {
        ELOG_ERROR(getDefaultLogger(), "Failed to create segmented log file target, out of memory");
        termGlobals();
        return ELOG_INVALID_TARGET_ID;
    }

    if (setLogTarget(logTarget) == ELOG_INVALID_TARGET_ID) {
        logTarget->stop();
        delete logTarget;
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

ELogTargetId ELogSystem::setLogTarget(ELogTarget* logTarget, bool printBanner /* = false */) {
    // first start the log target
    if (!logTarget->start()) {
        delete logTarget;
        ELOG_ERROR(getDefaultLogger(), "Failed to start log target");
        return ELOG_INVALID_TARGET_ID;
    }

    // check if this is the first log target or not
    if (!sLogTargets.empty()) {
        sLogTargets[0]->stop();
        delete sLogTargets[0];
        sLogTargets[0] = logTarget;
        return 0;
    } else {
        sLogTargets.push_back(logTarget);
        if (printBanner) {
            ELOG_INFO(getDefaultLogger(), "======================================================");
        }
        return (ELogTargetId)(sLogTargets.size() - 1);
    }
}

ELogTargetId ELogSystem::setFileLogTarget(const char* logFilePath, bool printBanner /* = true */) {
    // create new log target
    ELogFileTarget* logTarget = new (std::nothrow) ELogFileTarget(logFilePath, &sDefaultPolicy);
    if (logTarget == nullptr) {
        ELOG_ERROR(getDefaultLogger(), "Failed to create log target, out of memory");
        return ELOG_INVALID_TARGET_ID;
    }

    return ELogSystem::setLogTarget(logTarget, printBanner);
}

ELogTargetId ELogSystem::setFileLogTarget(FILE* fileHandle, bool printBanner /* = false */) {
    // create new log target
    ELogFileTarget* logTarget = new (std::nothrow) ELogFileTarget(fileHandle, &sDefaultPolicy);
    if (logTarget == nullptr) {
        ELOG_ERROR(getDefaultLogger(), "Failed to create log target, out of memory");
        return ELOG_INVALID_TARGET_ID;
    }

    return ELogSystem::setLogTarget(logTarget, printBanner);
}

ELogTargetId ELogSystem::addLogTarget(ELogTarget* logTarget) {
    if (!sLogTargets[0]->start()) {
        return ELOG_INVALID_TARGET_ID;
    }
    sLogTargets.push_back(logTarget);
    return (ELogTargetId)(sLogTargets.size() - 1);
}

ELogTargetId ELogSystem::addFileLogTarget(const char* logFilePath) {
    // create new log target
    ELogFileTarget* logTarget = new (std::nothrow) ELogFileTarget(logFilePath, &sDefaultPolicy);
    if (logTarget == nullptr) {
        ELOG_ERROR(getDefaultLogger(), "Failed to create log target, out of memory");
        return ELOG_INVALID_TARGET_ID;
    }

    return ELogSystem::addLogTarget(logTarget);
}

ELogTargetId ELogSystem::addFileLogTarget(FILE* fileHandle) {
    ELogFileTarget* logTarget = new (std::nothrow) ELogFileTarget(fileHandle, &sDefaultPolicy);
    if (logTarget == nullptr) {
        ELOG_ERROR(getDefaultLogger(), "Failed to create log target, out of memory");
        return ELOG_INVALID_TARGET_ID;
    }

    return addLogTarget(logTarget);
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
    addFileLogTarget(stderr);
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

// log sources
ELogSourceId ELogSystem::defineLogSource(const char* qualifiedName) {
    std::unique_lock<std::mutex> lock(sSourceTreeLock);
    // parse name to components and start traveling up to last component
    std::vector<std::string> namePath;
    parseSourceName(qualifiedName, namePath);

    ELogSource* currSource = sRootLogSource;
    uint32_t namecount = namePath.size();
    for (uint32_t i = 0; i < namecount - 1; ++i) {
        currSource = currSource->getChild(namePath[i].c_str());
        if (currSource == nullptr) {
            return ELOG_INVALID_SOURCE_ID;
        }
    }

    // make sure name does not exist already
    if (currSource->getChild(namePath.back().c_str()) != nullptr) {
        return ELOG_INVALID_SOURCE_ID;
    }

    ELogSource* logSource =
        new (std::nothrow) ELogSource(allocLogSourceId(), namePath.back().c_str(), currSource);
    if (!currSource->addChild(logSource)) {
        // impossible
        // TODO: we need an error listener to get all errors and deal with them, default is to
        // dump to stderr (no sense in defining error code, convert to string, etc.)
        delete logSource;
        return ELOG_INVALID_SOURCE_ID;
    }

    bool res =
        sLogSourceMap.insert(ELogSourceMap::value_type(logSource->getId(), logSource)).second;
    if (!res) {
        // internal error, roll back
        currSource->removeChild(logSource->getName());
        delete logSource;
    }
    return logSource->getId();
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
ELogLogger* ELogSystem::getLogger(const char* sourceName) {
    ELogLogger* logger = nullptr;
    ELogSource* source = getLogSource(sourceName);
    if (source != nullptr) {
        logger = source->createLogger();
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

// note: each log target controls its own log format

void ELogSystem::log(const ELogRecord& logRecord) {
    for (ELogTarget* logTarget : sLogTargets) {
        logTarget->log(logRecord);
    }
}

/** @brief Converts system error code to string. */
char* ELogSystem::sysErrorToStr(int sysErrorCode) {
#if defined(__MINGW32__) || defined(__MINGW64__)
    return strerror(sysErrorCode);
#else
    static thread_local char buf[256];
    return strerror_r(sysErrorCode, buf, 256);
#endif
}

#ifdef __WIN32__
char* ELogSystem::win32SysErrorToStr(unsigned long sysErrorCode) {
    LPSTR messageBuffer = nullptr;
    size_t size = ::FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, sysErrorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0,
        NULL);
    return messageBuffer;
}

void ELogSystem::win32FreeErrorStr(char* errStr) { ::LocalFree(errStr); }
#endif

}  // namespace elog
