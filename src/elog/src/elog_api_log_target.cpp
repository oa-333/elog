#include "elog_api.h"
#include "elog_internal.h"
#include "elog_report.h"
#include "file/elog_buffered_file_target.h"
#include "file/elog_file_schema_handler.h"
#include "file/elog_file_target.h"
#include "file/elog_segmented_file_target.h"
#include "sys/elog_syslog_target.h"
#include "sys/elog_win32_event_log_target.h"

#define ELOG_MAX_TARGET_COUNT 256ul

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogTargetApi)

static std::vector<ELogTarget*> sLogTargets;
static ELogTarget* sDefaultLogTarget = nullptr;

extern bool initLogTargets() {
    // NOTE: statistics disabled
    sDefaultLogTarget = new (std::nothrow) ELogFileTarget(stderr, nullptr, false);
    if (sDefaultLogTarget == nullptr) {
        ELOG_REPORT_ERROR("Failed to create default log target, out of memory");
        return false;
    }
    sDefaultLogTarget->setName("elog_default");
    if (!sDefaultLogTarget->start()) {
        ELOG_REPORT_ERROR("Failed to create default log target, out of memory");
        sDefaultLogTarget->destroy();
        sDefaultLogTarget = nullptr;
        return false;
    }
    ELOG_REPORT_TRACE("Default log target initialized");
    return true;
}

extern void termLogTargets() {
    if (sDefaultLogTarget != nullptr) {
        sDefaultLogTarget->stop();
        sDefaultLogTarget->destroy();
        sDefaultLogTarget = nullptr;
    }
}

ELogTargetId addLogTarget(ELogTarget* logTarget) {
    // TODO: should we guard against duplicate names (they are used in search by name and in log
    // affinity mask building) - this might require API change (returning at least bool)
    ELOG_REPORT_TRACE("Adding log target: %s", logTarget->getName());

    // we must start the log target early because of statistics dependency (if started after adding
    // to array, then any ELOG_REPORT_XXX in between would trigger logging for the new target before
    // the statistics object was created)
    // TODO: this is too sensitive, it is better to check whether the statistics object exists and
    // get rid of m_enableStats altogether
    if (!logTarget->start()) {
        ELOG_REPORT_ERROR("Failed to start log target %s", logTarget->getName());
        logTarget->setId(ELOG_INVALID_TARGET_ID);
        return ELOG_INVALID_TARGET_ID;
    }

    // find vacant slot ro add a new one
    ELogTargetId logTargetId = ELOG_INVALID_TARGET_ID;
    for (uint32_t i = 0; i < sLogTargets.size(); ++i) {
        if (sLogTargets[i] == nullptr) {
            sLogTargets[i] = logTarget;
            ELOG_REPORT_TRACE("Added log target %s with id %u", logTarget->getName(), i);
            logTargetId = i;
            break;
        }
    }

    // otherwise add a new slot
    if (logTargetId == ELOG_INVALID_TARGET_ID) {
        if (sLogTargets.size() == ELOG_MAX_TARGET_COUNT) {
            ELOG_REPORT_ERROR("Cannot add log target, reached hard limit of log targets %u",
                              ELOG_MAX_TARGET_COUNT);
            logTarget->stop();
            return ELOG_INVALID_TARGET_ID;
        }
        logTargetId = (ELogTargetId)sLogTargets.size();
        sLogTargets.push_back(logTarget);
        ELOG_REPORT_TRACE("Added log target %s with id %u", logTarget->getName(), logTargetId);
    }

    // set target id and start it
    logTarget->setId(logTargetId);

    // write accumulated log messages if there are such
    getPreInitLoggerRef().writeAccumulatedLogMessages(logTarget);
    return logTargetId;
}

ELogTargetId addLogFileTarget(const char* logFilePath, uint32_t bufferSize /* = 0 */,
                              bool useLock /* = false */, uint32_t segmentLimitMB /* = 0 */,
                              uint32_t segmentCount /* = 0 */, bool enableStats /* = true */,
                              ELogLevel logLevel /* = ELEVEL_INFO */,
                              ELogFlushPolicy* flushPolicy /* = nullptr */,
                              ELogFilter* logFilter /* = nullptr */,
                              ELogFormatter* logFormatter /* = nullptr */) {
    // we delegate to the schema handler
    ELogTarget* logTarget = ELogFileSchemaHandler::createLogTarget(
        logFilePath, bufferSize, useLock, segmentLimitMB, 0, segmentCount, enableStats);
    if (logTarget == nullptr) {
        return ELOG_INVALID_TARGET_ID;
    }

    logTarget->setLogLevel(logLevel);
    if (flushPolicy != nullptr) {
        logTarget->setFlushPolicy(flushPolicy);
    }
    if (logFilter != nullptr) {
        logTarget->setLogFilter(logFilter);
    }
    if (logFormatter != nullptr) {
        logTarget->setLogFormatter(logFormatter);
    }

    ELogTargetId logTargetId = addLogTarget(logTarget);
    if (logTargetId == ELOG_INVALID_TARGET_ID) {
        // NOTE: detach from policy/filter/formatter before delete, because in case of failure
        // caller is still owner of these objects.
        logTarget->detach();
        logTarget->destroy();
    }

    return logTargetId;
}

ELogTargetId attachLogFileTarget(FILE* fileHandle, bool closeHandleWhenDone /* = false */,
                                 uint32_t bufferSize /* = 0 */, bool useLock /* = false */,
                                 bool enableStats /* = true */,
                                 ELogLevel logLevel /* = ELEVEL_INFO */,
                                 ELogFlushPolicy* flushPolicy /* = nullptr */,
                                 ELogFilter* logFilter /* = nullptr */,
                                 ELogFormatter* logFormatter /* = nullptr */) {
    ELogTarget* logTarget = nullptr;
    if (bufferSize > 0) {
        logTarget = new (std::nothrow) ELogBufferedFileTarget(
            fileHandle, bufferSize, useLock, flushPolicy, closeHandleWhenDone, enableStats);
    } else {
        logTarget = new (std::nothrow)
            ELogFileTarget(fileHandle, flushPolicy, closeHandleWhenDone, enableStats);
    }
    if (logTarget == nullptr) {
        ELOG_REPORT_ERROR("Failed to create log target, out of memory");
        return ELOG_INVALID_TARGET_ID;
    }

    logTarget->setLogLevel(logLevel);
    if (flushPolicy != nullptr) {
        logTarget->setFlushPolicy(flushPolicy);
    }
    if (logFilter != nullptr) {
        logTarget->setLogFilter(logFilter);
    }
    if (logFormatter != nullptr) {
        logTarget->setLogFormatter(logFormatter);
    }

    ELogTargetId logTargetId = addLogTarget(logTarget);
    if (logTargetId == ELOG_INVALID_TARGET_ID) {
        // NOTE: detach from policy/filter/formatter before delete, because in case of failure
        // caller is still owner of these objects.
        logTarget->detach();
        logTarget->destroy();
    }

    return logTargetId;
}

ELogTargetId addStdErrLogTarget(ELogLevel logLevel /* = ELEVEL_INFO */,
                                ELogFilter* logFilter /* = nullptr */,
                                ELogFormatter* logFormatter /* = nullptr */) {
    return attachLogFileTarget(stderr, false, 0, false, false, logLevel, nullptr, logFilter,
                               logFormatter);
}

ELogTargetId addStdOutLogTarget(ELogLevel logLevel /* = ELEVEL_INFO */,
                                ELogFilter* logFilter /* = nullptr */,
                                ELogFormatter* logFormatter /* = nullptr */) {
    return attachLogFileTarget(stdout, false, 0, false, false, logLevel, nullptr, logFilter,
                               logFormatter);
}

ELogTargetId addSysLogTarget(ELogLevel logLevel /* = ELEVEL_INFO */,
                             ELogFilter* logFilter /* = nullptr */,
                             ELogFormatter* logFormatter /* = nullptr */) {
#ifdef ELOG_LINUX
    ELogSysLogTarget* logTarget = new (std::nothrow) ELogSysLogTarget();
    if (logTarget == nullptr) {
        ELOG_REPORT_ERROR("Failed to create syslog target, out of memory");
        return ELOG_INVALID_TARGET_ID;
    }

    logTarget->setLogLevel(logLevel);
    if (logFilter != nullptr) {
        logTarget->setLogFilter(logFilter);
    }
    if (logFormatter != nullptr) {
        logTarget->setLogFormatter(logFormatter);
    }

    ELogTargetId logTargetId = addLogTarget(logTarget);
    if (logTargetId == ELOG_INVALID_TARGET_ID) {
        // NOTE: detach from policy/filter/formatter before delete, because in case of failure
        // caller is still owner of these objects.
        logTarget->detach();
        logTarget->destroy();
    }

    return logTargetId;
#else
    ELOG_REPORT_ERROR("Cannot create syslog target: not supported on current platform");
    return ELOG_INVALID_TARGET_ID;
#endif
}

ELogTargetId addWin32EventLogTarget(ELogLevel logLevel /* = ELEVEL_INFO */,
                                    const char* eventSourceName /* = "" */,
                                    uint32_t eventId /* = 0 */,
                                    ELogFilter* logFilter /* = nullptr */,
                                    ELogFormatter* logFormatter /* = nullptr */) {
#ifdef ELOG_WINDOWS
    ELogWin32EventLogTarget* logTarget =
        new (std::nothrow) ELogWin32EventLogTarget(eventSourceName, eventId);
    if (logTarget == nullptr) {
        ELOG_REPORT_ERROR("Failed to create Windows Event Log target, out of memory");
        return ELOG_INVALID_TARGET_ID;
    }

    logTarget->setLogLevel(logLevel);
    if (logFilter != nullptr) {
        logTarget->setLogFilter(logFilter);
    }
    if (logFormatter != nullptr) {
        logTarget->setLogFormatter(logFormatter);
    }

    ELogTargetId logTargetId = addLogTarget(logTarget);
    if (logTargetId == ELOG_INVALID_TARGET_ID) {
        // NOTE: detach from policy/filter/formatter before delete, because in case of failure
        // caller is still owner of these objects.
        logTarget->detach();
        logTarget->destroy();
    }

    return logTargetId;
#else
    ELOG_REPORT_ERROR("Cannot create Windows Event Log target: not supported on current platform");
    return ELOG_INVALID_TARGET_ID;
#endif
}

ELogTargetId addTracer(const char* traceFilePath, uint32_t traceBufferSize, const char* targetName,
                       const char* sourceName) {
    // prepare configuration string
    std::stringstream s;
    s << "async://quantum?quantum_buffer_size=" << traceBufferSize << "&name=" << targetName
      << " | file:///" << traceFilePath << "?flush_policy=immediate";
    std::string cfg = s.str();

    // add log target from configuration string
    ELogTargetId id = configureLogTarget(cfg.c_str());
    if (id == ELOG_INVALID_TARGET_ID) {
        return id;
    }
    ELogTarget* logTarget = getLogTarget(id);
    if (logTarget == nullptr) {
        ELOG_REPORT_ERROR("Internal error while adding tracer, log target by id %u not found", id);
        return false;
    }

    // define a pass key to the trace target, so that normal log messages will not reach the tracer
    logTarget->setPassKey();

    // define log source
    ELogSource* logSource = defineLogSource(sourceName, true);
    if (logSource == nullptr) {
        ELOG_REPORT_ERROR("Failed to define tracer %s log source by name %s", targetName,
                          sourceName);
        return false;
    }

    // bind log source to target using affinity mask
    ELogTargetAffinityMask mask;
    ELOG_CLEAR_TARGET_AFFINITY_MASK(mask);
    ELOG_ADD_TARGET_AFFINITY_MASK(mask, logTarget->getId());
    logSource->setLogTargetAffinity(mask);

    // add pass key to the log source
    logSource->addPassKey(logTarget->getPassKey());
    return id;
}

ELogTarget* getLogTarget(ELogTargetId targetId) {
    if (targetId >= sLogTargets.size()) {
        return nullptr;
    }
    return sLogTargets[targetId];
}

ELogTarget* getLogTarget(const char* logTargetName) {
    for (ELogTarget* logTarget : sLogTargets) {
        if (strcmp(logTarget->getName(), logTargetName) == 0) {
            return logTarget;
        }
    }
    return nullptr;
}

ELogTargetId getLogTargetId(const char* logTargetName) {
    for (uint32_t logTargetId = 0; logTargetId < sLogTargets.size(); ++logTargetId) {
        ELogTarget* logTarget = sLogTargets[logTargetId];
        if (strcmp(logTarget->getName(), logTargetName) == 0) {
            return logTargetId;
        }
    }
    return ELOG_INVALID_TARGET_ID;
}

static void compactLogTargets() {
    // find largest suffix of removed log targets
    size_t maxLogTargetId = sLogTargets.size() - 1;
    bool done = false;
    while (!done) {
        if (sLogTargets[maxLogTargetId] != nullptr) {
            sLogTargets.resize(maxLogTargetId + 1);
            ELOG_REPORT_TRACE("Log target array compacted to %zu entries", sLogTargets.size());
            done = true;
        } else if (maxLogTargetId == 0) {
            // last one is also null
            sLogTargets.clear();
            ELOG_REPORT_TRACE("Log target array fully truncated");
            done = true;
        } else {
            --maxLogTargetId;
        }
    }
}

void removeLogTarget(ELogTargetId targetId) {
    // be careful, if this is the last log target, we must put back stderr
    if (targetId >= sLogTargets.size()) {
        ELOG_REPORT_ERROR("Cannot remove log target %u, id out of range", targetId);
        return;
    }

    if (sLogTargets[targetId] == nullptr) {
        ELOG_REPORT_ERROR("Cannot remove log target %u, not found", targetId);
        return;
    }

    // delete the log target and put null
    // we cannot shrink the vector because that will change log target indices
    ELogTarget* logTarget = sLogTargets[targetId];
    ELOG_REPORT_TRACE("Removing log target %s at %p", logTarget->getName(), logTarget);
    logTarget->stop();
    logTarget->destroy();
    sLogTargets[targetId] = nullptr;

    // if suffix entries contain nulls we can reduce array size
    compactLogTargets();
}

void clearAllLogTargets() {
    // NOTE: since log target may have indirect dependencies (e.g. one log target, while writing a
    // log message, issues another log message which is dispatched to all other log targets), we
    // first stop all targets and then delete them, but this requires log target to be able to
    // reject log messages after stop() was called.
    for (ELogTarget* logTarget : sLogTargets) {
        if (isTerminating() || (logTarget != nullptr && !logTarget->isSystemTarget())) {
            logTarget->stop();
        }
    }
    for (uint32_t i = 0; i < sLogTargets.size(); ++i) {
        ELogTarget* logTarget = sLogTargets[i];
        if (isTerminating() || (logTarget != nullptr && !logTarget->isSystemTarget())) {
            logTarget->destroy();
            sLogTargets[i] = nullptr;
        }
    }
    if (!sLogTargets.empty()) {
        compactLogTargets();
    }
}

void removeLogTarget(ELogTarget* target) { removeLogTarget(target->getId()); }

void resetThreadStatCounters(uint64_t slotId) {
    for (ELogTargetId logTargetId = 0; logTargetId < sLogTargets.size(); ++logTargetId) {
        ELogTarget* logTarget = sLogTargets[logTargetId];
        ELogStats* stats = logTarget->getStats();
        if (stats != nullptr) {
            stats->resetThreadCounters(slotId);
        }
    }
}

void logMsgTarget(const ELogRecord& logRecord, ELogTargetAffinityMask logTargetAffinityMask) {
    bool logged = false;
    for (ELogTargetId logTargetId = 0; logTargetId < sLogTargets.size(); ++logTargetId) {
        ELogTarget* logTarget = sLogTargets[logTargetId];
        if (logTargetId > ELOG_MAX_LOG_TARGET_ID_AFFINITY ||
            ELOG_HAS_TARGET_AFFINITY_MASK(logTargetAffinityMask, logTargetId)) {
            // check also pass key if present
            ELogPassKey passKey = logTarget->getPassKey();
            if (passKey == ELOG_NO_PASSKEY ||
                logRecord.m_logger->getLogSource()->hasPassKey(passKey)) {
                logTarget->log(logRecord);
                logged = true;
            }
        }
    }

    // by default, if no log target is defined yet, log is redirected to stderr
    if (!logged) {
        if (sDefaultLogTarget != nullptr) {
            sDefaultLogTarget->log(logRecord);
        } else {
            fprintf(stderr, "%s\n", logRecord.m_logMsg);
        }
    }
}

}  // namespace elog