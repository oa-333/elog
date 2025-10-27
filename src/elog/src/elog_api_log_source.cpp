#include <atomic>
#include <regex>
#include <unordered_map>

#include "elog_api.h"
#include "elog_common.h"
#include "elog_common_def.h"
#include "elog_internal.h"
#include "elog_report.h"

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogSourceApi)

static std::mutex sSourceTreeLock;
static ELogSource* sRootLogSource = nullptr;
typedef std::unordered_map<ELogSourceId, ELogSource*> ELogSourceMap;
static ELogSourceMap sLogSourceMap;
static std::atomic<ELogSourceId> sNextLogSourceId;

extern ELogSource* createLogSource(ELogSourceId sourceId, const char* name,
                                   ELogSource* parent = nullptr, ELogLevel logLevel = ELEVEL_INFO);
extern void deleteLogSource(ELogSource* logSource);
static ELogSource* addChildSource(ELogSource* parent, const char* sourceName);

inline ELogSourceId allocLogSourceId() {
    return sNextLogSourceId.fetch_add(1, std::memory_order_relaxed);
}

bool initLogSources() {
    // root logger has no name
    sRootLogSource = createLogSource(allocLogSourceId(), "");
    if (sRootLogSource == nullptr) {
        ELOG_REPORT_ERROR("Failed to create root log source, out of memory");
        return false;
    }
    ELOG_REPORT_TRACE("Root log source initialized");

    // add to global map
    bool res =
        sLogSourceMap.insert(ELogSourceMap::value_type(sRootLogSource->getId(), sRootLogSource))
            .second;
    if (!res) {
        ELOG_REPORT_ERROR(
            "Failed to insert root log source to global source map (duplicate found)");
        return false;
    }
    ELOG_REPORT_TRACE("Root log source added to global log source map");
    return true;
}

void termLogSources() {
    if (sRootLogSource != nullptr) {
        deleteLogSource(sRootLogSource);
        sRootLogSource = nullptr;
    }
    sLogSourceMap.clear();
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

ELogSource* createLogSource(ELogSourceId sourceId, const char* name,
                            ELogSource* parent /* = nullptr */,
                            ELogLevel logLevel /* = ELEVEL_INFO */) {
    return new (std::nothrow) ELogSource(sourceId, name, parent, logLevel);
}

// control carefully who can delete a log source
void deleteLogSource(ELogSource* logSource) { delete logSource; }

ELogSource* addChildSource(ELogSource* parent, const char* sourceName) {
    ELogSource* logSource = createLogSource(allocLogSourceId(), sourceName, parent);
    if (logSource == nullptr) {
        ELOG_REPORT_ERROR("Failed to create log source %s, out of memory", sourceName);
        return nullptr;
    }
    if (!parent->addChild(logSource)) {
        // impossible
        ELOG_REPORT_ERROR("Internal error, cannot add child source %s, already exists", sourceName);
        deleteLogSource(logSource);
        return nullptr;
    }

    bool res =
        sLogSourceMap.insert(ELogSourceMap::value_type(logSource->getId(), logSource)).second;
    if (!res) {
        // internal error, roll back
        ELOG_REPORT_ERROR("Internal error, cannot add new log source %s by id %u, already exists",
                          sourceName, logSource->getId());
        parent->removeChild(logSource->getName());
        deleteLogSource(logSource);
        return nullptr;
    }
    return logSource;
}

// log sources
ELogSource* defineLogSource(const char* qualifiedName, bool defineMissingPath /* = true */) {
    if (qualifiedName == nullptr || *qualifiedName == 0) {
        return sRootLogSource;
    }
    std::unique_lock<std::mutex> lock(sSourceTreeLock);
    // parse name to components and start traveling up to last component
    std::vector<std::string> namePath;
    parseSourceName(qualifiedName, namePath);

    ELogSource* currSource = sRootLogSource;
    size_t nameCount = namePath.size();
    for (size_t i = 0; i < nameCount - 1; ++i) {
        ELogSource* childSource = currSource->getChild(namePath[i].c_str());
        if (childSource == nullptr && defineMissingPath) {
            childSource = addChildSource(currSource, namePath[i].c_str());
        }
        if (childSource == nullptr) {
            // TODO: partial failures are left as dangling sources, is that ok?
            if (defineMissingPath) {
                ELOG_REPORT_ERROR("Failed to define log source %s: failed to define path part %s",
                                  qualifiedName, namePath[i].c_str());
            } else {
                ELOG_REPORT_ERROR("Cannot define log source %s: missing path part %s",
                                  qualifiedName, namePath[i].c_str());
            }
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
    if (logSource == nullptr) {
        ELOG_REPORT_ERROR("Failed to define log source %s: failed to add child %s to parent %s",
                          logSourceName, currSource->getQualifiedName());
        return nullptr;
    }

    // in case of a new log source, we check if there is an environment variable for configuring
    // its log level. The expected format is: <qualified-log-source-name>_log_level =
    // <elog-level> every dot in the qualified name is replaced with underscore
    std::string envVarName = std::string(qualifiedName) + "_log_level";
    std::replace(envVarName.begin(), envVarName.end(), '.', '_');
    std::string envVarValue;
    if (elog_getenv(envVarName.c_str(), envVarValue)) {
        ELogLevel logLevel = ELEVEL_INFO;
        if (elogLevelFromStr(envVarValue.c_str(), logLevel)) {
            logSource->setLogLevel(logLevel, ELogPropagateMode::PM_NONE);
        }
    }

    return logSource;
}

ELogSource* getLogSource(const char* qualifiedName) {
    std::unique_lock<std::mutex> lock(sSourceTreeLock);
    std::vector<std::string> namePath;
    parseSourceName(qualifiedName, namePath);
    ELogSource* currSource = sRootLogSource;
    size_t nameCount = namePath.size();
    for (size_t i = 0; i < nameCount; ++i) {
        currSource = currSource->getChild(namePath[i].c_str());
        if (currSource == nullptr) {
            ELOG_REPORT_ERROR("Cannot retrieve log source %s: missing path part %s", qualifiedName,
                              namePath[i].c_str());
            break;
        }
    }
    return currSource;
}

ELogSource* getLogSource(ELogSourceId logSourceId) {
    ELogSource* logSource = nullptr;
    std::unique_lock<std::mutex> lock(sSourceTreeLock);
    ELogSourceMap::iterator itr = sLogSourceMap.find(logSourceId);
    if (itr != sLogSourceMap.end()) {
        logSource = itr->second;
    }
    return logSource;
}

ELogSource* getRootLogSource() { return sRootLogSource; }

void getLogSources(const char* logSourceRegEx, std::vector<ELogSource*>& logSources) {
    std::regex pattern(logSourceRegEx);
    std::unique_lock<std::mutex> lock(sSourceTreeLock);
    ELogSourceMap::iterator itr = sLogSourceMap.begin();
    while (itr != sLogSourceMap.end()) {
        if (std::regex_match(itr->second->getQualifiedName(), pattern)) {
            logSources.emplace_back(itr->second);
        }
        ++itr;
    }
}

void getLogSourcesEx(const char* includeRegEx, const char* excludeRegEx,
                     std::vector<ELogSource*>& logSources) {
    std::regex includePattern(includeRegEx);
    std::regex excludePattern(excludeRegEx);
    std::unique_lock<std::mutex> lock(sSourceTreeLock);
    ELogSourceMap::iterator itr = sLogSourceMap.begin();
    while (itr != sLogSourceMap.end()) {
        if (std::regex_match(itr->second->getQualifiedName(), includePattern) &&
            !std::regex_match(itr->second->getQualifiedName(), excludePattern)) {
            logSources.emplace_back(itr->second);
        }
        ++itr;
    }
}

void visitLogSources(const char* includeRegEx, const char* excludeRegEx,
                     ELogSourceVisitor* visitor) {
    bool hasIncludeRegEx = (includeRegEx != nullptr && *includeRegEx != 0);
    bool hasExcludeRegEx = (excludeRegEx != nullptr && *excludeRegEx != 0);
    std::regex includePattern(hasIncludeRegEx ? includeRegEx : ".*");
    std::regex excludePattern(hasExcludeRegEx ? excludeRegEx : "");
    std::unique_lock<std::mutex> lock(sSourceTreeLock);
    for (auto& entry : sLogSourceMap) {
        // check log source name qualifies by inclusion filter
        // AND NOT disqualified by exclusion filter
        if ((!hasIncludeRegEx ||
             std::regex_match(entry.second->getQualifiedName(), includePattern)) &&
            (!hasExcludeRegEx ||
             !std::regex_match(entry.second->getQualifiedName(), excludePattern))) {
            visitor->onLogSource(entry.second);
        }
    }
}

// void configureLogSourceLevel(const char* qualifiedName, ELogLevel logLevel);

}  // namespace elog
