#include "elog_config_loader.h"

#include <fstream>

#include "elog_common.h"
#include "elog_deferred_target.h"
#include "elog_error.h"
#include "elog_filter.h"
#include "elog_formatter.h"
#include "elog_quantum_target.h"
#include "elog_queued_target.h"
#include "elog_schema_manager.h"

namespace elog {

bool ELogConfigLoader::loadFileProperties(const char* configPath, ELogPropertySequence& props) {
    // use simple format
    std::ifstream cfgFile(configPath);
    if (!cfgFile.good()) {
        ELOG_REPORT_SYS_ERROR(fopen, "Failed to open configuration file for reading: %s",
                              configPath);
        return false;
    }

    std::string line;
    while (std::getline(cfgFile, line)) {
        // skip comment lines
        if (trim(line)[0] == '#') {
            continue;
        }
        // parse line
        std::istringstream is_line(line);
        std::string key;
        if (std::getline(is_line, key, '=')) {
            std::string value;
            if (std::getline(is_line, value)) {
                std::string trimmedKey = trim(key);
                std::string trimmedValue = trim(value);
                props.push_back(std::make_pair(trimmedKey, trimmedValue));
            }
        }
    }

    // TODO: support multiline spec
    return true;
}

ELogTarget* ELogConfigLoader::loadLogTarget(const std::string& logTargetCfg,
                                            const ELogTargetNestedSpec& logTargetNestedSpec,
                                            ELogTargetSpecStyle specStyle) {
    ELogSchemaHandler* schemaHandler =
        ELogSchemaManager::getSchemaHandler(logTargetNestedSpec.m_spec.m_scheme.c_str());
    if (schemaHandler != nullptr) {
        ELogTarget* logTarget = schemaHandler->loadTarget(logTargetCfg, logTargetNestedSpec);
        if (logTarget == nullptr) {
            ELOG_REPORT_ERROR("Failed to load target for scheme %s: %s",
                              logTargetNestedSpec.m_spec.m_scheme.c_str(), logTargetCfg.c_str());
            return nullptr;
        }

        // in case of nested style, there no need to apply compound target, the schema handler
        // already loads it nested (this is actually done in recursive manner, schema handler calls
        // configureLogTarget for each sub target, which in turn activates the schema handler again)
        if (specStyle == ELOG_STYLE_URL) {
            // apply compound target
            bool errorOccurred = false;
            ELogTarget* compoundTarget = applyCompoundTarget(
                logTarget, logTargetCfg, logTargetNestedSpec.m_spec, errorOccurred);
            if (errorOccurred) {
                ELOG_REPORT_ERROR("Failed to apply compound log target specification");
                delete logTarget;
                return nullptr;
            }
            if (compoundTarget != nullptr) {
                logTarget = compoundTarget;
            }
        }

        // configure common properties (just this target, not recursively nested)
        if (!configureLogTargetCommon(logTarget, logTargetCfg, logTargetNestedSpec)) {
            delete logTarget;
            return nullptr;
        }
        return logTarget;
    }

    ELOG_REPORT_ERROR("Invalid log target specification, unrecognized scheme %s: %s",
                      logTargetNestedSpec.m_spec.m_scheme.c_str(), logTargetCfg.c_str());
    return nullptr;
}

ELogFlushPolicy* ELogConfigLoader::loadFlushPolicy(const std::string& logTargetCfg,
                                                   const ELogTargetNestedSpec& logTargetNestedSpec,
                                                   bool allowNone, bool& result) {
    result = true;
    ELogPropertyMap::const_iterator itr = logTargetNestedSpec.m_spec.m_props.find("flush_policy");
    if (itr == logTargetNestedSpec.m_spec.m_props.end()) {
        // it is ok not to find a flush policy, in this ok result is true but flush policy is null
        return nullptr;
    }

    const std::string& flushPolicyCfg = itr->second;
    if (flushPolicyCfg.compare("none") == 0) {
        // special case, let target decide by itself what happens when no flush policy is set
        if (!allowNone) {
            ELOG_REPORT_ERROR("None flush policy is not allowed in this context");
            result = false;
        }
        return nullptr;
    }

    ELogFlushPolicy* flushPolicy = constructFlushPolicy(flushPolicyCfg.c_str());
    if (flushPolicy == nullptr) {
        ELOG_REPORT_ERROR("Failed to create flush policy by type %s: %s", flushPolicyCfg.c_str(),
                          logTargetCfg.c_str());
        result = false;
        return nullptr;
    }

    if (!flushPolicy->load(logTargetCfg, logTargetNestedSpec)) {
        ELOG_REPORT_ERROR("Failed to load flush policy by properties %s: %s",
                          flushPolicyCfg.c_str(), logTargetCfg.c_str());
        result = false;
        delete flushPolicy;
        flushPolicy = nullptr;
    }
    return flushPolicy;
}

ELogFilter* ELogConfigLoader::loadLogFilter(const std::string& logTargetCfg,
                                            const ELogTargetNestedSpec& logTargetNestedSpec,
                                            bool& result) {
    result = true;
    ELogFilter* filter = nullptr;
    ELogPropertyMap::const_iterator itr = logTargetNestedSpec.m_spec.m_props.find("filter");
    if (itr != logTargetNestedSpec.m_spec.m_props.end()) {
        const std::string& filterCfg = itr->second;
        filter = constructFilter(filterCfg.c_str());
        if (filter == nullptr) {
            ELOG_REPORT_ERROR("Failed to create filter by type %s: %s", filterCfg.c_str(),
                              logTargetCfg.c_str());
            result = false;
        } else if (!filter->load(logTargetCfg, logTargetNestedSpec)) {
            ELOG_REPORT_ERROR("Failed to load filter by properties %s: %s", filterCfg.c_str(),
                              logTargetCfg.c_str());
            delete filter;
            filter = nullptr;
            result = false;
        }
    }
    return filter;
}

bool ELogConfigLoader::configureLogTargetCommon(ELogTarget* logTarget,
                                                const std::string& logTargetCfg,
                                                const ELogTargetNestedSpec& logTargetSpec) {
    // apply target name if any
    applyTargetName(logTarget, logTargetSpec.m_spec);

    // apply target log level if any
    if (!applyTargetLogLevel(logTarget, logTargetCfg, logTargetSpec.m_spec)) {
        return false;
    }

    // apply log format if any
    if (!applyTargetLogFormat(logTarget, logTargetCfg, logTargetSpec.m_spec)) {
        return false;
    }

    // apply flush policy if any
    if (!applyTargetFlushPolicy(logTarget, logTargetCfg, logTargetSpec)) {
        return false;
    }

    // apply filter if any
    if (!applyTargetFilter(logTarget, logTargetCfg, logTargetSpec)) {
        return false;
    }
    return true;
}

void ELogConfigLoader::applyTargetName(ELogTarget* logTarget, const ELogTargetSpec& logTargetSpec) {
    ELogPropertyMap::const_iterator itr = logTargetSpec.m_props.find("name");
    if (itr != logTargetSpec.m_props.end()) {
        logTarget->setName(itr->second.c_str());
    }
}

bool ELogConfigLoader::applyTargetLogLevel(ELogTarget* logTarget, const std::string& logTargetCfg,
                                           const ELogTargetSpec& logTargetSpec) {
    ELogPropertyMap::const_iterator itr = logTargetSpec.m_props.find("log_level");
    if (itr != logTargetSpec.m_props.end()) {
        ELogLevel logLevel = ELEVEL_INFO;
        if (!elogLevelFromStr(itr->second.c_str(), logLevel)) {
            ELOG_REPORT_ERROR("Invalid log level '%s' specified in log target: %s",
                              itr->second.c_str(), logTargetCfg.c_str());
            return false;
        }
        logTarget->setLogLevel(logLevel);
    }
    return true;
}

bool ELogConfigLoader::applyTargetLogFormat(ELogTarget* logTarget, const std::string& logTargetCfg,
                                            const ELogTargetSpec& logTargetSpec) {
    ELogPropertyMap::const_iterator itr = logTargetSpec.m_props.find("log_format");
    if (itr != logTargetSpec.m_props.end()) {
        ELogFormatter* logFormatter = new (std::nothrow) ELogFormatter();
        if (!logFormatter->initialize(itr->second.c_str())) {
            ELOG_REPORT_ERROR("Invalid log format '%s' specified in log target: %s",
                              itr->second.c_str(), logTargetCfg.c_str());
            delete logFormatter;
            return false;
        }
        logTarget->setLogFormatter(logFormatter);
    }
    return true;
}

bool ELogConfigLoader::applyTargetFlushPolicy(ELogTarget* logTarget,
                                              const std::string& logTargetCfg,
                                              const ELogTargetNestedSpec& logTargetSpec) {
    bool result = false;
    ELogFlushPolicy* flushPolicy = loadFlushPolicy(logTargetCfg, logTargetSpec, true, result);
    if (!result) {
        return false;
    }
    if (flushPolicy != nullptr) {
        // active policies require a log target
        if (flushPolicy->isActive()) {
            flushPolicy->setLogTarget(logTarget);
        }

        // that's it
        logTarget->setFlushPolicy(flushPolicy);
    }
    return true;
    /*ELogPropertyMap::const_iterator itr = logTargetSpec.m_spec.m_props.find("flush_policy");
    if (itr != logTargetSpec.m_spec.m_props.end()) {
        const std::string& flushPolicyCfg = itr->second;
        if (flushPolicyCfg.compare("none") == 0) {
            // special case, let target decide by itself what happens when no flush policy is set
            return true;
        }
        ELogFlushPolicy* flushPolicy = constructFlushPolicy(flushPolicyCfg.c_str());
        if (flushPolicy == nullptr) {
            ELOG_REPORT_ERROR("Failed to create flush policy by type %s: %s",
    flushPolicyCfg.c_str(), logTargetCfg.c_str()); return false;
        }
        if (!flushPolicy->load(logTargetCfg, logTargetSpec)) {
            ELOG_REPORT_ERROR("Failed to load flush policy by properties %s: %s",
    flushPolicyCfg.c_str(), logTargetCfg.c_str()); delete flushPolicy; return false;
        }

        // active policies require a log target
        if (flushPolicy->isActive()) {
            flushPolicy->setLogTarget(logTarget);
        }

        // that's it
        logTarget->setFlushPolicy(flushPolicy);*/

    /*if (flushPolicyCfg.compare("immediate") == 0) {
        flushPolicy = new (std::nothrow) ELogImmediateFlushPolicy();
    } else if (flushPolicyCfg.compare("never") == 0) {
        flushPolicy = new (std::nothrow) ELogNeverFlushPolicy();
    } else if (flushPolicyCfg.compare("count") == 0) {
        ELogPropertyMap::const_iterator itr2 = logTargetSpec.m_props.find("flush_count");
        if (itr2 == logTargetSpec.m_props.end()) {
            ELOG_REPORT_ERROR(
                "Invalid flush policy configuration, missing expected flush_count property for "
                "flush_policy=count: %s",
                logTargetCfg.c_str());
            return false;
        }
        uint32_t logCountLimit = 0;
        if (!parseIntProp("flush_count", logTargetCfg, itr2->second, logCountLimit, true)) {
            ELOG_REPORT_ERROR(
                "Invalid flush policy configuration, flush_count property value '%s' is an "
                "ill-formed integer: %s",
                itr2->second.c_str(), logTargetCfg.c_str());
            return false;
        }
        flushPolicy = new (std::nothrow) ELogCountFlushPolicy(logCountLimit);
    } else if (flushPolicyCfg.compare("size") == 0) {
        ELogPropertyMap::const_iterator itr2 = logTargetSpec.m_props.find("flush_size_bytes");
        if (itr2 == logTargetSpec.m_props.end()) {
            ELOG_REPORT_ERROR(
                "Invalid flush policy configuration, missing expected flush_size_bytes "
                "property for flush_policy=size: %s",
                logTargetCfg.c_str());
            return false;
        }
        uint32_t logSizeLimitBytes = 0;
        if (!parseIntProp("flush_size_bytes", logTargetCfg, itr2->second, logSizeLimitBytes,
                          true)) {
            ELOG_REPORT_ERROR(
                "Invalid flush policy configuration, flush_size_bytes property value '%s' is "
                "an ill-formed integer: %s",
                itr2->second.c_str(), logTargetCfg.c_str());
            return false;
        }
        flushPolicy = new (std::nothrow) ELogSizeFlushPolicy(logSizeLimitBytes);
    } else if (flushPolicyCfg.compare("time") == 0) {
        ELogPropertyMap::const_iterator itr2 =
            logTargetSpec.m_props.find("flush_timeout_millis");
        if (itr2 == logTargetSpec.m_props.end()) {
            ELOG_REPORT_ERROR(
                "Invalid flush policy configuration, missing expected flush_timeout_millis "
                "property for flush_policy=time: %s",
                logTargetCfg.c_str());
            return false;
        }
        uint32_t logTimeLimitMillis = 0;
        if (!parseIntProp("flush_timeout_millis", logTargetCfg, itr2->second,
                          logTimeLimitMillis, true)) {
            ELOG_REPORT_ERROR(
                "Invalid flush policy configuration, flush_timeout_millis property value '%s' "
                "is an ill-formed integer: %s",
                itr2->second.c_str(), logTargetCfg.c_str());
            return false;
        }
        flushPolicy = new (std::nothrow) ELogTimedFlushPolicy(logTimeLimitMillis, logTarget);
    } else if (flushPolicyCfg.compare("none") != 0) {
        ELOG_REPORT_ERROR("Unrecognized flush policy configuration %s: %s", flushPolicyCfg.c_str(),
                    logTargetCfg.c_str());
        return false;
    }
    logTarget->setFlushPolicy(flushPolicy);*/
    //}
    // return true;
}

bool ELogConfigLoader::applyTargetFilter(ELogTarget* logTarget, const std::string& logTargetCfg,
                                         const ELogTargetNestedSpec& logTargetSpec) {
    bool result = false;
    ELogFilter* filter = loadLogFilter(logTargetCfg, logTargetSpec, result);
    if (!result) {
        return false;
    }
    if (filter != nullptr) {
        logTarget->setLogFilter(filter);
    }
    return true;
    /*ELogPropertyMap::const_iterator itr = logTargetSpec.m_spec.m_props.find("filter");
    if (itr != logTargetSpec.m_spec.m_props.end()) {
        const std::string& filterCfg = itr->second;
        ELogFilter* filter = constructFilter(filterCfg.c_str());
        if (filter == nullptr) {
            ELOG_REPORT_ERROR("Failed to create filter by type %s: %s", filterCfg.c_str(),
                        logTargetCfg.c_str());
            return false;
        }
        if (!filter->load(logTargetCfg, logTargetSpec)) {
            ELOG_REPORT_ERROR("Failed to load filter by properties %s: %s", filterCfg.c_str(),
                        logTargetCfg.c_str());
            delete filter;
            return false;
        }

        // that's it
        logTarget->setLogFilter(filter);
    }
    return true;*/
}

ELogTarget* ELogConfigLoader::applyCompoundTarget(ELogTarget* logTarget,
                                                  const std::string& logTargetCfg,
                                                  const ELogTargetSpec& logTargetSpec,
                                                  bool& errorOccurred) {
    // there could be optional poperties: deferred,
    // queue_batch_size=<batch-size>,queue_timeout_millis=<timeout-millis>
    // quantum_buffer_size=<buffer-size>, quantum-congestion-policy=wait/discard
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
                ELOG_REPORT_ERROR(
                    "Deferred log target cannot be specified with queued or quantum target: %s",
                    logTargetCfg.c_str());
                return nullptr;
            }
            if (deferred) {
                ELOG_REPORT_ERROR("Deferred log target can be specified only once: %s",
                                  logTargetCfg.c_str());
                return nullptr;
            }
            deferred = true;
        }

        // parse queue batch size property
        else if (prop.first.compare("queue_batch_size") == 0) {
            if (deferred || quantumBufferSize > 0) {
                ELOG_REPORT_ERROR(
                    "Queued log target cannot be specified with deferred or quantum target: %s",
                    logTargetCfg.c_str());
                return nullptr;
            }
            if (queueBatchSize > 0) {
                ELOG_REPORT_ERROR("Queue batch size can be specified only once: %s",
                                  logTargetCfg.c_str());
                return nullptr;
            }
            if (!parseIntProp("queue_batch_size", logTargetCfg, prop.second, queueBatchSize)) {
                return nullptr;
            }
        }

        // parse queue timeout millis property
        else if (prop.first.compare("queue_timeout_millis") == 0) {
            if (deferred || quantumBufferSize > 0) {
                ELOG_REPORT_ERROR(
                    "Queued log target cannot be specified with deferred or quantum target: %s",
                    logTargetCfg.c_str());
                return nullptr;
            }
            if (queueTimeoutMillis > 0) {
                ELOG_REPORT_ERROR("Queue timeout millis can be specified only once: %s",
                                  logTargetCfg.c_str());
                return nullptr;
            }
            if (!parseIntProp("queue_timeout_millis", logTargetCfg, prop.second,
                              queueTimeoutMillis)) {
                return nullptr;
            }
        }

        // parse quantum buffer size
        else if (prop.first.compare("quantum_buffer_size") == 0) {
            if (deferred || queueBatchSize > 0 || queueTimeoutMillis > 0) {
                ELOG_REPORT_ERROR(
                    "Quantum log target cannot be specified with deferred or queued target: %s",
                    logTargetCfg.c_str());
                return nullptr;
            }
            if (quantumBufferSize > 0) {
                ELOG_REPORT_ERROR("Quantum buffer size can be specified only once: %s",
                                  logTargetCfg.c_str());
                return nullptr;
            }
            if (!parseIntProp("quantum_buffer_size", logTargetCfg, prop.second,
                              quantumBufferSize)) {
                return nullptr;
            }
        }

        // parse quantum buffer size
        else if (prop.first.compare("quantum-congestion-policy") == 0) {
            if (deferred || queueBatchSize > 0 || queueTimeoutMillis > 0) {
                ELOG_REPORT_ERROR(
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
                ELOG_REPORT_ERROR("Invalid quantum log target congestion policy value '%s': %s",
                                  prop.second.c_str(), logTargetCfg.c_str());
                return nullptr;
            }
        }
    }

    if (queueBatchSize > 0 && queueTimeoutMillis == 0) {
        ELOG_REPORT_ERROR("Missing queue_timeout_millis parameter in log target specification: %s",
                          logTargetCfg.c_str());
        return nullptr;
    }

    if (queueBatchSize == 0 && queueTimeoutMillis > 0) {
        ELOG_REPORT_ERROR("Missing queue_batch_size parameter in log target specification: %s",
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

}  // namespace elog