#include "elog_api.h"
#include "elog_api_time_source.h"
#include "elog_config_loader.h"
#include "elog_config_parser.h"
#include "elog_internal.h"
#include "elog_level_cfg.h"
#include "elog_report.h"
#include "elog_time_source.h"

#ifdef ELOG_ENABLE_LIFE_SIGN
#include "elog_api_life_sign.h"
#endif

// configure configuration service and restart it
#ifdef ELOG_ENABLE_CONFIG_SERVICE
#include "cfg_srv/elog_api_config_service.h"
#endif

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogConfigApi)

static bool configureLogTargetImpl(const std::string& logTargetCfg, ELogTargetId* id = nullptr);
static bool configureLogTargetNode(const ELogConfigMapNode* logTargetCfg,
                                   ELogTargetId* id = nullptr);
static bool augmentConfigFromEnv(ELogConfigMapNode* cfgMap);

bool configureLogTargetImpl(const std::string& logTargetCfg, ELogTargetId* id /* = nullptr */) {
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
    // queue_batch_size=<batch-size>,queue_timeout_millis=<timeout-millis>
    // quantum_buffer_size=<buffer-size>
    //
    // future provision:
    // tcp://host:port
    // udp://host:port
    // db://db-name?conn_string=<conn-string>&insert-statement=<insert-statement>
    // msgq://message-broker-name?conn_string=<conn-string>&queue=<queue-name>&msgq_topic=<topic-name>
    //
    // additionally the following nested format is accepted:
    //
    // log_target = { scheme=db, db-name=postgresql, ...}
    // log_target = { scheme = async, type = deferred, log_target = { scheme = file, path = ...}}
    // log_target = { scheme = async, type = quantum, quantum_buffer_size = 10000,
    //      log_target = [{ scheme = file, path = ...}, {}, {}]}
    //
    // in theory nesting level is not restricted, but it doesn't make sense to have more than 2

    // load the target (common properties already configured)
    ELogTarget* logTarget = ELogConfigLoader::loadLogTarget(logTargetCfg.c_str());
    if (logTarget == nullptr) {
        return false;
    }

    // finally add the log target
    if (addLogTarget(logTarget) == ELOG_INVALID_TARGET_ID) {
        ELOG_REPORT_ERROR("Failed to add log target %s with scheme %s", logTarget->getName(),
                          logTarget->getTypeName());
        logTarget->destroy();
        return false;
    }
    if (id != nullptr) {
        *id = logTarget->getId();
    }
    return true;
}

bool configureLogTargetNode(const ELogConfigMapNode* logTargetCfg,
                            ELogTargetId* id /* = nullptr */) {
    // load the target (common properties already configured)
    ELogTarget* logTarget = ELogConfigLoader::loadLogTarget(logTargetCfg);
    if (logTarget == nullptr) {
        return false;
    }

    // finally add the log target
    if (addLogTarget(logTarget) == ELOG_INVALID_TARGET_ID) {
        ELOG_REPORT_ERROR("Failed to add log target %s with scheme %s (context: %s)",
                          logTarget->getName(), logTarget->getTypeName(),
                          logTargetCfg->getFullContext());
        logTarget->destroy();
        return false;
    }
    if (id != nullptr) {
        *id = logTarget->getId();
    }
    return true;
}

bool configureByPropFile(const char* configPath, bool defineLogSources /* = true */,
                         bool defineMissingPath /* = true */) {
    // elog requires properties in order due to log level propagation
    ELogPropertySequence props;
    if (!ELogConfigLoader::loadFileProperties(configPath, props)) {
        return false;
    }
    return configureByProps(props, defineLogSources, defineMissingPath);
}

bool configureByProps(const ELogPropertySequence& props, bool defineLogSources /* = true */,
                      bool defineMissingPath /* = true */) {
    // TODO: Allow override from env also log_format, log_filter, and perhaps global flush policy

    // configure log format (unrelated to order of appearance)
    // NOTE: only one such item is expected
    std::string logFormatCfg;
    if (getProp(props, ELOG_FORMAT_CONFIG_NAME, logFormatCfg)) {
        if (!configureLogFormat(logFormatCfg.c_str())) {
            ELOG_REPORT_ERROR("Invalid log format in properties: %s", logFormatCfg.c_str());
            return false;
        }
    }

    // configure global filter
    std::string logFilterCfg;
    if (getProp(props, ELOG_FILTER_CONFIG_NAME, logFilterCfg)) {
        if (!configureLogFilter(logFilterCfg.c_str())) {
            return false;
        }
    }

    // configure global rate limit (overrides global filter)
    std::string rateLimitCfg;
    if (getProp(props, ELOG_RATE_LIMIT_CONFIG_NAME, rateLimitCfg)) {
        if (!configureRateLimit(rateLimitCfg.c_str())) {
            return false;
        }
    }

    // configure global log level format (font/color)
    /*std::string logLevelFormatCfg;
    if (getProp(props, ELOG_LEVEL_FORMAT_CONFIG_NAME, logLevelFormatCfg)) {
        if (!configureLogLevelFormat(logLevelFormatCfg.c_str())) {
            return false;
        }
    }*/

    std::vector<ELogLevelCfg> logLevelCfg;
    ELogLevel logLevel = ELEVEL_INFO;
    ELogPropagateMode propagateMode = ELogPropagateMode::PM_NONE;

    const char* logLevelSuffix1 = "." ELOG_LEVEL_CONFIG_NAME;  // for configuration files
    const char* logLevelSuffix2 = "_" ELOG_LEVEL_CONFIG_NAME;  // for environment variables
    size_t logLevelSuffixLen = strlen(logLevelSuffix1);

    const char* logAffinitySuffix1 = "." ELOG_AFFINITY_CONFIG_NAME;  // for configuration files
    const char* logAffinitySuffix2 = "_" ELOG_AFFINITY_CONFIG_NAME;  // for environment variables
    size_t logAffinitySuffixLen = strlen(logAffinitySuffix1);

    // get configuration also from env
    ELogPropertySequence envProps;
    char** envPtr = environ;
    for (; *envPtr; envPtr++) {
        std::string envVar(*envPtr);
        std::string::size_type equalPos = envVar.find('=');
        if (equalPos != std::string::npos) {
            std::string envVarName = envVar.substr(0, equalPos);
            std::string envVarValue = envVar.substr(equalPos + 1);
            if (envVarName.ends_with(ELOG_LEVEL_CONFIG_NAME)) {
                ELOG_REPORT_TRACE("Adding prop %s = %s from env", envVarName.c_str(),
                                  envVarValue.c_str());
                envProps.push_back({envVarName, envVarValue});
            }
            if (envVarName.ends_with(ELOG_AFFINITY_CONFIG_NAME)) {
                ELOG_REPORT_TRACE("Adding prop %s = %s from env", envVarName.c_str(),
                                  envVarValue.c_str());
                envProps.push_back({envVarName, envVarValue});
            }
        }
    }

    // prepare combined properties, let env vars override property file
    ELogPropertySequence combinedProps;
    combinedProps.insert(combinedProps.end(), props.begin(), props.end());
    combinedProps.insert(combinedProps.end(), envProps.begin(), envProps.end());

    for (const ELogProperty& prop : combinedProps) {
        // check if this is root log level
        if (prop.first.compare(ELOG_LEVEL_CONFIG_NAME) == 0) {
            // global log level
            if (!ELogConfigParser::parseLogLevel(prop.second.c_str(), logLevel, propagateMode)) {
                ELOG_REPORT_ERROR("Invalid global log level: %s", prop.second.c_str());
                return false;
            }
            logLevelCfg.push_back({getRootLogSource(), logLevel, propagateMode});
            continue;
        }

        // check for log target
        if (prop.first.compare(ELOG_TARGET_CONFIG_NAME) == 0) {
            // configure log target
            if (!configureLogTargetImpl(prop.second)) {
                return false;
            }
            continue;
        }

        // configure log levels of log sources
        // search for ".log_level" with a dot, in order to filter out global log_level key
        // NOTE: when definining log sources, we must first define all log sources, then set log
        // level to configured level and apply log level propagation. If we apply propagation
        // before child log sources are defined, then propagation is lost.
        if (prop.first.ends_with(logLevelSuffix1) || prop.first.ends_with(logLevelSuffix2)) {
            const std::string& key = prop.first;
            // shave off trailing ".log_level/_log_level" (that includes dot/underscore)
            std::string sourceName = key.substr(0, key.size() - logLevelSuffixLen);
            ELogSource* logSource = defineLogSources
                                        ? defineLogSource(sourceName.c_str(), defineMissingPath)
                                        : getLogSource(sourceName.c_str());
            if (logSource == nullptr) {
                ELOG_REPORT_ERROR("Invalid log source name: %s", sourceName.c_str());
                return false;
            }
            propagateMode = ELogPropagateMode::PM_NONE;
            if (!ELogConfigParser::parseLogLevel(prop.second.c_str(), logLevel, propagateMode)) {
                ELOG_REPORT_ERROR("Invalid source %s log level: %s", sourceName.c_str(),
                                  prop.second.c_str());
                return false;
            }
            logLevelCfg.push_back({logSource, logLevel, propagateMode});
        }

        // configure log affinity of log sources
        if (prop.first.ends_with(logAffinitySuffix1) || prop.first.ends_with(logAffinitySuffix2)) {
            const std::string& key = prop.first;
            // shave off trailing ".log_level/_log_level" (that includes dot/underscore)
            std::string sourceName = key.substr(0, key.size() - logAffinitySuffixLen);
            ELogSource* logSource = defineLogSources
                                        ? defineLogSource(sourceName.c_str(), defineMissingPath)
                                        : getLogSource(sourceName.c_str());
            if (logSource == nullptr) {
                ELOG_REPORT_ERROR("Invalid log source name: %s", sourceName.c_str());
                return false;
            }
            ELogTargetAffinityMask mask = 0;
            if (!ELogConfigParser::parseLogAffinityList(prop.second.c_str(), mask)) {
                ELOG_REPORT_ERROR("Invalid source %s log affinity specification: %s",
                                  sourceName.c_str(), prop.second.c_str());
                return false;
            }
            logSource->setLogTargetAffinity(mask);
        }
    }

    // now we can apply log level propagation
    for (const ELogLevelCfg& cfg : logLevelCfg) {
        ELOG_REPORT_TRACE("Setting %s log level to %s (propagate - %u)",
                          cfg.m_logSource->getQualifiedName(), elogLevelToStr(cfg.m_logLevel),
                          (uint32_t)cfg.m_propagationMode);
        cfg.m_logSource->setLogLevel(cfg.m_logLevel, cfg.m_propagationMode);
    }

    // configure life-sign report settings
#ifdef ELOG_ENABLE_LIFE_SIGN
    if (!configLifeSignProps(props)) {
        return false;
    }
#endif

    // configure configuration service and restart it
#ifdef ELOG_ENABLE_CONFIG_SERVICE
    if (!configConfigServiceProps(props)) {
        return false;
    }
#endif

    // configure time source
    if (!configTimeSourceProps(props)) {
        return false;
    }

    return true;
}

bool configureByPropFileEx(const char* configPath, bool defineLogSources /* = true */,
                           bool defineMissingPath /* = true */) {
    // elog requires properties in order due to log level propagation
    ELogConfig* config = ELogConfig::loadFromPropFile(configPath);
    if (config == nullptr) {
        ELOG_REPORT_ERROR("Failed to load configuration from properties file: %s", configPath);
        return false;
    }
    bool res = configure(config, defineLogSources, defineMissingPath);
    delete config;
    return res;
}

bool configureByPropsEx(const ELogPropertyPosSequence& props, bool defineLogSources /* = true */,
                        bool defineMissingPath /* = true */) {
    // we first convert properties to configuration object and then load from cfg object
    ELogConfig* config = ELogConfig::loadFromProps(props);
    if (config == nullptr) {
        ELOG_REPORT_ERROR("Failed to load configuration from properties");
        return false;
    }
    bool res = configure(config, defineLogSources, defineMissingPath);
    delete config;
    return res;
}

bool configureByFile(const char* configPath, bool defineLogSources /* = true */,
                     bool defineMissingPath /* = true */) {
    // elog requires properties in order due to log level propagation
    ELogConfig* config = ELogConfig::loadFromFile(configPath);
    if (config == nullptr) {
        ELOG_REPORT_ERROR("Failed to load configuration from file: %s", configPath);
        return false;
    }
    bool res = configure(config, defineLogSources, defineMissingPath);
    delete config;
    return res;
}

bool augmentConfigFromEnv(ELogConfigMapNode* cfgMap) {
    char** envPtr = environ;
    for (; *envPtr; envPtr++) {
        std::string envVar(*envPtr);
        std::string::size_type equalPos = envVar.find('=');
        if (equalPos != std::string::npos) {
            std::string envVarName = envVar.substr(0, equalPos);
            std::string envVarValue = envVar.substr(equalPos + 1);

            // check for log_level, log_format, log_filter, log_rate_limit
            if (envVarName.compare(ELOG_LEVEL_CONFIG_NAME) == 0 ||
                envVarName.compare(ELOG_FORMAT_CONFIG_NAME) == 0 ||
                envVarName.compare(ELOG_FILTER_CONFIG_NAME) == 0) {
                ELOG_REPORT_TRACE("Overriding %s from env: %s", envVarName.c_str(),
                                  envVarValue.c_str());
                if (!cfgMap->mergeStringEntry(envVarName.c_str(), envVarValue.c_str())) {
                    ELOG_REPORT_ERROR("Failed to merge %s from environment variables (context: %s)",
                                      envVarName.c_str(), cfgMap->getFullContext());
                    return false;
                }
            } else if (envVarName.compare(ELOG_RATE_LIMIT_CONFIG_NAME) == 0) {
                ELOG_REPORT_TRACE("Overriding " ELOG_RATE_LIMIT_CONFIG_NAME " from env: %s",
                                  envVarValue.c_str());
                int64_t rateLimit = 0;
                if (!parseIntProp(ELOG_RATE_LIMIT_CONFIG_NAME, "N/A", envVarValue, rateLimit)) {
                    ELOG_REPORT_ERROR("Invalid " ELOG_RATE_LIMIT_CONFIG_NAME
                                      " environment variable value %s, expecting integer "
                                      "(context: %s)",
                                      envVarValue.c_str(), cfgMap->getFullContext());
                    return false;
                }
                if (!cfgMap->mergeIntEntry(envVarName.c_str(), rateLimit)) {
                    ELOG_REPORT_ERROR("Failed to merge " ELOG_RATE_LIMIT_CONFIG_NAME
                                      " from environment variables (context: %s)",
                                      cfgMap->getFullContext());
                    return false;
                }
            }

            // check for variables that end with _log_level or _log_affinity
            else if (envVarName.ends_with(ELOG_LEVEL_CONFIG_NAME) ||
                     envVarName.ends_with(ELOG_AFFINITY_CONFIG_NAME)) {
                ELOG_REPORT_TRACE("Overriding %s = %s from env", envVarName.c_str(),
                                  envVarValue.c_str());
                if (!cfgMap->mergeStringEntry(envVarName.c_str(), envVarValue.c_str())) {
                    ELOG_REPORT_ERROR("Failed to merge %s from environment variables (context: %s)",
                                      envVarName.c_str(), cfgMap->getFullContext());
                    return false;
                }
            }
        }
    }
    return true;
}

bool configureByStr(const char* configStr, bool defineLogSources /* = true */,
                    bool defineMissingPath /* = true */) {
    ELogConfig* config = ELogConfig::loadFromString(configStr);
    if (config == nullptr) {
        ELOG_REPORT_ERROR("Failed to load configuration from string: %s", configStr);
        return false;
    }
    bool res = configure(config, defineLogSources, defineMissingPath);
    delete config;
    return res;
}

bool configure(ELogConfig* config, bool defineLogSources /* = true */,
               bool defineMissingPath /* = true */) {
    // verify root node is of map type
    if (config->getRootNode()->getNodeType() != ELogConfigNodeType::ELOG_CONFIG_MAP_NODE) {
        ELOG_REPORT_ERROR("Top-level configuration node is not a map node");
        return false;
    }
    ELogConfigMapNode* cfgMap =
        const_cast<ELogConfigMapNode*>((const ELogConfigMapNode*)config->getRootNode());

    // augment with environment variables
    if (!augmentConfigFromEnv(cfgMap)) {
        ELOG_REPORT_ERROR("Failed to augment configuration object from environment variables");
        return false;
    }

    // now configure

    // configure global log format
    bool found = false;
    std::string logFormatCfg;
    if (!cfgMap->getStringValue(ELOG_FORMAT_CONFIG_NAME, found, logFormatCfg)) {
        // configuration error
        return false;
    } else if (found && !configureLogFormat(logFormatCfg.c_str())) {
        ELOG_REPORT_ERROR("Invalid top-level log format in properties: %s", logFormatCfg.c_str());
        return false;
    }

    // configure global filter
    std::string logFilterCfg;
    if (!cfgMap->getStringValue(ELOG_FILTER_CONFIG_NAME, found, logFilterCfg)) {
        // configuration error
        return false;
    } else if (found && !configureLogFilter(logFilterCfg.c_str())) {
        ELOG_REPORT_ERROR("Invalid top-level log filter in properties: %s", logFilterCfg.c_str());
        return false;
    }

    // configure global rate limit (overrides global filter)
    // TODO: what about valid values? should be defined and checked
    std::string rateLimitCfg;
    if (!cfgMap->getStringValue(ELOG_RATE_LIMIT_CONFIG_NAME, found, rateLimitCfg)) {
        // configuration error
        return false;
    } else if (found && !configureRateLimit(rateLimitCfg.c_str())) {
        return false;
    }

    std::vector<ELogLevelCfg> logLevelCfg;
    ELogLevel logLevel = ELEVEL_INFO;
    ELogPropagateMode propagateMode = ELogPropagateMode::PM_NONE;

    const char* logLevelSuffix1 = "." ELOG_LEVEL_CONFIG_NAME;  // for configuration files
    const char* logLevelSuffix2 = "_" ELOG_LEVEL_CONFIG_NAME;  // for environment variables
    size_t logLevelSuffixLen = strlen(logLevelSuffix1);

    const char* logAffinitySuffix1 = "." ELOG_AFFINITY_CONFIG_NAME;  // for configuration files
    const char* logAffinitySuffix2 = "_" ELOG_AFFINITY_CONFIG_NAME;  // for environment variables
    size_t logAffinitySuffixLen = strlen(logAffinitySuffix1);

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

        // check for log target
        if (prop.first.compare(ELOG_TARGET_CONFIG_NAME) == 0) {
            // configure log target
            if (cfgValue->getValueType() == ELogConfigValueType::ELOG_CONFIG_STRING_VALUE) {
                const char* logTargetStr =
                    ((const ELogConfigStringValue*)cfgValue)->getStringValue();
                if (!configureLogTargetImpl(logTargetStr)) {
                    ELOG_REPORT_ERROR("Failed to configure log target (context: %s)",
                                      cfgValue->getFullContext());
                    return false;
                }
            } else if (cfgValue->getValueType() == ELogConfigValueType::ELOG_CONFIG_MAP_VALUE) {
                const ELogConfigMapNode* logTargetCfg =
                    ((const ELogConfigMapValue*)cfgValue)->getMapNode();
                if (!configureLogTargetNode(logTargetCfg)) {
                    return false;
                }
            } else {
                ELOG_REPORT_ERROR("Invalid configuration value type for " ELOG_TARGET_CONFIG_NAME
                                  ", expecting either string or "
                                  "map, seeing instead %s (context: %s)",
                                  configValueTypeToString(cfgValue->getValueType()),
                                  cfgValue->getFullContext());
                return false;
            }
            continue;
        }

        // configure log levels of log sources
        // search for ".log_level" with a dot, in order to filter out global log_level key
        // NOTE: when definining log sources, we must first define all log sources, then set log
        // level to configured level and apply log level propagation. If we apply propagation
        // before child log sources are defined, then propagation is lost.
        if (prop.first.ends_with(logLevelSuffix1) || prop.first.ends_with(logLevelSuffix2)) {
            const std::string& key = prop.first;
            // shave off trailing ".log_level/_log_level" (that includes dot/underscore)
            std::string sourceName = key.substr(0, key.size() - logLevelSuffixLen);
            std::replace(sourceName.begin(), sourceName.end(), '_', '.');
            ELogSource* logSource = defineLogSources
                                        ? defineLogSource(sourceName.c_str(), defineMissingPath)
                                        : getLogSource(sourceName.c_str());
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

        // configure log affinity of log sources
        if (prop.first.ends_with(logAffinitySuffix1) || prop.first.ends_with(logAffinitySuffix2)) {
            const std::string& key = prop.first;
            // shave off trailing ".log_level/_log_level" (that includes dot/underscore)
            std::string sourceName = key.substr(0, key.size() - logAffinitySuffixLen);
            ELogSource* logSource = defineLogSources
                                        ? defineLogSource(sourceName.c_str(), defineMissingPath)
                                        : getLogSource(sourceName.c_str());
            if (logSource == nullptr) {
                ELOG_REPORT_ERROR("Invalid log source name: %s", sourceName.c_str());
                return false;
            }
            ELogTargetAffinityMask mask = 0;
            // get log level, should be a string
            if (!validateConfigValueStringType(cfgValue, key.c_str())) {
                return false;
            }
            const char* logAffinityStr = ((const ELogConfigStringValue*)cfgValue)->getStringValue();
            if (!ELogConfigParser::parseLogAffinityList(logAffinityStr, mask)) {
                ELOG_REPORT_ERROR("Invalid source %s log affinity specification: %s",
                                  sourceName.c_str(), logAffinityStr);
                return false;
            }
            logSource->setLogTargetAffinity(mask);
        }
    }

    // now we can apply log level propagation
    for (const ELogLevelCfg& cfg : logLevelCfg) {
        ELOG_REPORT_TRACE("Setting %s log level to %s (propagate - %u)",
                          cfg.m_logSource->getQualifiedName(), elogLevelToStr(cfg.m_logLevel),
                          (uint32_t)cfg.m_propagationMode);
        cfg.m_logSource->setLogLevel(cfg.m_logLevel, cfg.m_propagationMode);
    }

#ifdef ELOG_USING_COMM_UTIL
    refreshCommUtilLogLevelCfg();
#endif

// configure life-sign report settings
#ifdef ELOG_ENABLE_LIFE_SIGN
    if (!configLifeSign(cfgMap)) {
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

ELogTargetId configureLogTarget(const char* logTargetCfg) {
    ELogTargetId id = ELOG_INVALID_TARGET_ID;
    if (!configureLogTargetImpl(logTargetCfg, &id)) {
        return ELOG_INVALID_TARGET_ID;
    }
    return id;
}

}  // namespace elog