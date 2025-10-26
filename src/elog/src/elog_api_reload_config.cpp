#include "elog_api_reload_config.h"

#ifdef ELOG_ENABLE_RELOAD_CONFIG

#include <condition_variable>
#include <mutex>
#include <thread>

#include "elog_api.h"
#ifdef ELOG_ENABLE_CONFIG_SERVICE
#include "cfg_srv/elog_api_config_service.h"
#endif
#include "elog_api_life_sign.h"
#include "elog_api_time_source.h"
#include "elog_config_loader.h"
#include "elog_config_parser.h"
#include "elog_field_selector_internal.h"
#include "elog_internal.h"
#include "elog_level_cfg.h"
#include "elog_report.h"

#ifdef ELOG_LINUX
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace elog {

static std::thread sReloadConfigThread;
static std::mutex sReloadConfigLock;
static std::condition_variable sReloadConfigCV;
static bool sStopReloadConfig = false;

ELOG_DECLARE_REPORT_LOGGER(ELogConfigReloadApi)

enum ReloadAction { START_RELOAD_THREAD, STOP_RELOAD_THREAD, NOTIFY_THREAD, NO_ACTION };

static bool execReloadAction(ReloadAction action, const char* configFilePath,
                             bool resetReloadPeriod = false);
static bool shouldStopReloadConfig();
static uint64_t getFileModifyTime(const char* filePath);
static bool reconfigure(ELogConfig* config);

bool reloadConfigFile(const char* configPath /* = nullptr */) {
    // we reload only log levels and nothing else
    // future versions may allow adding log sources or log targets

    std::string usedConfigPath;
    if (configPath == nullptr) {
        std::unique_lock<std::mutex> lock(sReloadConfigLock);
        usedConfigPath = getParams().m_configFilePath;
    } else {
        usedConfigPath = configPath;
    }

    if (usedConfigPath.empty()) {
        ELOG_REPORT_ERROR(
            "Cannot reload configuration, no file path specified, and ELog was not initialized "
            "with a configuration file");
        return false;
    }

    ELogConfig* config = ELogConfig::loadFromFile(usedConfigPath.c_str());
    if (config == nullptr) {
        ELOG_REPORT_ERROR("Failed to reload configuration from file: %s", configPath);
        return false;
    }
    bool res = reconfigure(config);
    delete config;
    return res;
}

bool reloadConfigStr(const char* configStr) {
    // we reload only log levels and nothing else
    // future versions may allow adding log sources or log targets

    ELogConfig* config = ELogConfig::loadFromString(configStr);
    if (config == nullptr) {
        ELOG_REPORT_ERROR("Failed to reload configuration from string: %s", configStr);
        return false;
    }
    bool res = reconfigure(config);
    delete config;
    return res;
}

bool execReloadAction(ReloadAction action, const char* configFilePath,
                      bool resetReloadPeriod /* = false */) {
    if (action == START_RELOAD_THREAD) {
        ELOG_REPORT_TRACE("Loading configuration from: %s", configFilePath);
        if (!configureByFile(configFilePath)) {
            ELOG_REPORT_ERROR("Failed to load configuration from %s, ELog initialization aborted",
                              configFilePath);
            return false;
        }
        startReloadConfigThread();
    } else if (action == STOP_RELOAD_THREAD) {
        stopReloadConfigThread();
        if (resetReloadPeriod) {
            std::unique_lock<std::mutex> lock(sReloadConfigLock);
            modifyParams().m_reloadPeriodMillis = 0;
        }
    } else if (action == NOTIFY_THREAD) {
        sReloadConfigCV.notify_one();
    }

    return true;
}

bool setPeriodicReloadConfigFile(const char* configFilePath) {
    ReloadAction action = NO_ACTION;

    bool isEmptyFile = (configFilePath == nullptr || *configFilePath == 0);
    {
        std::unique_lock<std::mutex> lock(sReloadConfigLock);
        if (isEmptyFile) {
            if (getParams().m_configFilePath.empty()) {
                ELOG_REPORT_TRACE(
                    "Request to reset configuration reload file ignored, configuration file path "
                    "is already empty");
            } else {
                action = STOP_RELOAD_THREAD;
            }
        } else {
            if (getParams().m_configFilePath.empty()) {
                if (getParams().m_reloadPeriodMillis == 0) {
                    ELOG_REPORT_TRACE(
                        "Postponing launch of configuration reload thread until a reload period is "
                        "provided");
                } else {
                    action = START_RELOAD_THREAD;
                }
            } else {
                action = NOTIFY_THREAD;
            }
        }
        modifyParams().m_configFilePath = configFilePath;
    }

    return execReloadAction(action, configFilePath);
}

bool setReloadConfigPeriodMillis(uint64_t reloadPeriodMillis) {
    ReloadAction action = NO_ACTION;
    std::string configFilePath;

    {
        std::unique_lock<std::mutex> lock(sReloadConfigLock);
        if (reloadPeriodMillis == getParams().m_reloadPeriodMillis) {
            ELOG_REPORT_TRACE("Request to update configuration reload period to %" PRIu64
                              " milliseconds ignored, value is the same",
                              reloadPeriodMillis);
        } else if (getParams().m_reloadPeriodMillis == 0) {
            modifyParams().m_reloadPeriodMillis = reloadPeriodMillis;
            if (!getParams().m_configFilePath.empty()) {
                configFilePath = getParams().m_configFilePath;
                action = START_RELOAD_THREAD;
            } else {
                ELOG_REPORT_TRACE(
                    "Postponing launch of configuration reload thread until a configuration file "
                    "is provided");
            }
        } else {
            if (reloadPeriodMillis == 0) {
                // do not update period yet, otherwise config update thread might enter a tight loop
                action = STOP_RELOAD_THREAD;
            } else {
                modifyParams().m_reloadPeriodMillis = reloadPeriodMillis;
                action = NOTIFY_THREAD;
            }
        }
    }

    return execReloadAction(action, configFilePath.c_str(), true);
}

bool shouldStopReloadConfig() {
    std::unique_lock<std::mutex> lock(sReloadConfigLock);
    return sStopReloadConfig;
}

void startReloadConfigThread() {
    // launch config reload thread
    sReloadConfigThread = std::thread([]() {
        setCurrentThreadNameField("reload-config");
        // get configuration file path (thread-safe, allowing concurrent updates)
        std::string configFilePath;
        {
            std::unique_lock<std::mutex> lock(sReloadConfigLock);
            configFilePath = getParams().m_configFilePath;
        }
        ELOG_REPORT_TRACE("Starting periodic configuration loading from %s, every %u milliseconds",
                          configFilePath.c_str(), getParams().m_reloadPeriodMillis);

        uint64_t lastFileModifyTime = getFileModifyTime(configFilePath.c_str());
        while (!shouldStopReloadConfig()) {
            // interruptible sleep until next reload check
            {
                std::unique_lock<std::mutex> lock(sReloadConfigLock);
                sReloadConfigCV.wait_for(
                    lock, std::chrono::milliseconds(getParams().m_reloadPeriodMillis),
                    []() { return sStopReloadConfig; });
                if (sStopReloadConfig) {
                    break;
                }

                // NOTE: still holding lock
                // update current config file path
                // if file changed then reset last modify time
                if (configFilePath.compare(getParams().m_configFilePath) != 0) {
                    lastFileModifyTime = 0;
                    configFilePath = getParams().m_configFilePath;
                }
            }

            uint64_t fileModifyTime = getFileModifyTime(configFilePath.c_str());
            if (fileModifyTime > lastFileModifyTime) {
                reloadConfigFile();
                lastFileModifyTime = fileModifyTime;
            }
        }
    });
}

void stopReloadConfigThread() {
    ELOG_REPORT_TRACE("Stopping periodic configuration loading thread");
    {
        std::unique_lock<std::mutex> lock(sReloadConfigLock);
        sStopReloadConfig = true;
        sReloadConfigCV.notify_one();
    }
    sReloadConfigThread.join();
    ELOG_REPORT_TRACE("Periodic configuration loading thread stopped");
}

uint64_t getFileModifyTime(const char* filePath) {
    // NOTE: we don't care that in each platform the time units are different (as well as the
    // reference epoch)
#ifdef ELOG_WINDOWS
    WIN32_FILE_ATTRIBUTE_DATA attrs = {};
    if (!GetFileAttributesExA(filePath, GetFileExInfoStandard, (LPVOID)&attrs)) {
        ELOG_REPORT_WIN32_ERROR(GetFileAttributesExA, "Failed to get file attributes for: %s",
                                filePath);
        return 0;
    }
    // return time in 100-nano units
    return static_cast<uint64_t>(attrs.ftLastWriteTime.dwHighDateTime) << 32 |
           attrs.ftLastWriteTime.dwLowDateTime;
#else
    struct stat fileStat;
    if (stat(filePath, &fileStat) == -1) {
        ELOG_REPORT_SYS_ERROR(stat, "Failed to get file status for: %s", filePath);
        return 0;
    }
    // return time in seconds
    return fileStat.st_mtime;
#endif
}

// TODO: duplicate code - need to refactor
bool reconfigure(ELogConfig* config) {
    // verify root node is of map type
    if (config->getRootNode()->getNodeType() != ELogConfigNodeType::ELOG_CONFIG_MAP_NODE) {
        ELOG_REPORT_ERROR("Top-level configuration node is not a map node");
        return false;
    }
    const ELogConfigMapNode* cfgMap = (const ELogConfigMapNode*)config->getRootNode();

    std::vector<ELogLevelCfg> logLevelCfg;
    ELogLevel logLevel = ELEVEL_INFO;
    ELogPropagateMode propagateMode = ELogPropagateMode::PM_NONE;

    const char* logLevelSuffix = "." ELOG_LEVEL_CONFIG_NAME;
    size_t logLevelSuffixLen = strlen(logLevelSuffix);

    for (size_t i = 0; i < cfgMap->getEntryCount(); ++i) {
        const ELogConfigMapNode::EntryType& prop = cfgMap->getEntryAt(i);
        const ELogConfigValue* cfgValue = prop.second;
        // check if this is root log level
        if (prop.first.compare(ELOG_LEVEL_CONFIG_NAME) == 0) {
            // global log level, should be a string
            if (!validateConfigValueStringType(cfgValue, ELOG_LEVEL_CONFIG_NAME)) {
                return false;
            }
            const char* logLevelStr = ((const ELogConfigStringValue*)cfgValue)->getStringValue();
            if (!ELogConfigParser::parseLogLevel(logLevelStr, logLevel, propagateMode)) {
                ELOG_REPORT_ERROR("Invalid global log level: %s", logLevelStr);
                return false;
            }
            // configure later when we have all information gathered
            logLevelCfg.push_back({getRootLogSource(), logLevel, propagateMode});
            continue;
        }

        // configure log levels of log sources
        // search for ".log_level" with a dot, in order to filter out global log_level key
        if (prop.first.ends_with(logLevelSuffix)) {
            const std::string& key = prop.first;
            // shave off trailing ".log_level/_log_level" (that includes dot/underscore)
            std::string sourceName = key.substr(0, key.size() - logLevelSuffixLen);
            ELogSource* logSource = getLogSource(sourceName.c_str());
            if (logSource == nullptr) {
                ELOG_REPORT_ERROR("Invalid log source name: %s", sourceName.c_str());
                return false;
            }
            propagateMode = ELogPropagateMode::PM_NONE;
            // get log level, should be a string
            if (!validateConfigValueStringType(cfgValue, key.c_str())) {
                return false;
            }
            const char* logLevelStr = ((const ELogConfigStringValue*)cfgValue)->getStringValue();
            if (!ELogConfigParser::parseLogLevel(logLevelStr, logLevel, propagateMode)) {
                ELOG_REPORT_ERROR("Invalid source %s log level: %s", sourceName.c_str(),
                                  logLevelStr);
                return false;
            }
            logLevelCfg.push_back({logSource, logLevel, propagateMode});
        }
    }

    // now we can apply log level propagation
    for (const ELogLevelCfg& cfg : logLevelCfg) {
        ELOG_REPORT_TRACE("Setting %s log level to %s (propagate - %u)",
                          cfg.m_logSource->getQualifiedName(), elogLevelToStr(cfg.m_logLevel),
                          (uint32_t)cfg.m_propagationMode);
        cfg.m_logSource->setLogLevel(cfg.m_logLevel, cfg.m_propagationMode);
    }

#ifdef ELOG_ENABLE_LIFE_SIGN
    // configure life-sign report settings
    if (!configLifeSignBasic(cfgMap)) {
        return false;
    }
#endif

#ifdef ELOG_ENABLE_CONFIG_SERVICE
    if (!configConfigService(cfgMap)) {
        return false;
    }
#endif

    // configure time source (allow override from env)
    if (!configTimeSource(cfgMap)) {
        return false;
    }

    return true;
}

}  // namespace elog

#endif  // ELOG_ENABLE_RELOAD_CONFIG