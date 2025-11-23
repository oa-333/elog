#include "elog_api_log_target.h"

#include "elog_api.h"
#include "elog_internal.h"
#include "elog_report.h"
#include "file/elog_buffered_file_target.h"
#include "file/elog_file_schema_handler.h"
#include "file/elog_file_target.h"
#include "file/elog_segmented_file_target.h"
#include "sys/elog_syslog_target.h"
#include "sys/elog_win32_event_log_target.h"

#ifdef ELOG_ENABLE_DYNAMIC_CONFIG
#include "elog_atomic.h"
#include "elog_gc.h"
#endif

#define ELOG_MAX_TARGET_COUNT 256ul

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogTargetApi)

#ifdef ELOG_ENABLE_DYNAMIC_CONFIG
static std::vector<ELogAtomic<ELogTarget*>> sLogTargets;
static ELogGC* sLogTargetGC = nullptr;
static std::atomic<uint64_t> sLogTargetEpoch = 0;
#else
static std::vector<ELogTarget*> sLogTargets;
#endif
static ELogTarget* sDefaultLogTarget = nullptr;

/** @def A log target pointer denoting a reserved slot. */
#define ELOG_TARGET_RESERVED ((ELogTarget*)(-1ll))

bool initLogTargets() {
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
#ifdef ELOG_ENABLE_DYNAMIC_CONFIG
    sLogTargets.resize(getParams().m_maxLogTargets);

    // create garbage collector
    sLogTargetEpoch.store(0, std::memory_order_relaxed);
    sLogTargetGC = new (std::nothrow) ELogGC();
    if (sLogTargetGC == nullptr) {
        ELOG_REPORT_ERROR("Failed to allocate log target garbage collector, out of memory");
        termLogTargets();
        return false;
    }

    // initialize garbage collector
    if (!sLogTargetGC->initialize("elog_target_gc", getMaxThreads(), 0,
                                  getParams().m_logTargetGCPeriodMillis,
                                  getParams().m_logTargetGCTaskCount)) {
        ELOG_REPORT_ERROR("Failed to initialize log target garbage collector");
        termLogTargets();
        return false;
    }
    // NOTE: starting the background GC threads is postponed to a later phase, otherwise we get an
    // early call to the life sign manager before it was started
#endif
    return true;
}

void termLogTargets() {
#ifdef ELOG_ENABLE_DYNAMIC_CONFIG
    // terminate GC
    if (sLogTargetGC != nullptr) {
        sLogTargetGC->stop();
        if (!sLogTargetGC->destroy()) {
            ELOG_REPORT_ERROR("Failed to destroy log target garbage collector");
            return;
        }
        delete sLogTargetGC;
        sLogTargetGC = nullptr;
    }
#endif
    if (sDefaultLogTarget != nullptr) {
        sDefaultLogTarget->stop();
        sDefaultLogTarget->destroy();
        sDefaultLogTarget = nullptr;
    }
}

#ifdef ELOG_ENABLE_DYNAMIC_CONFIG
void startLogTargetGC() {
    if (sLogTargetGC != nullptr) {
        sLogTargetGC->start();
    }
}

ELOG_IMPLEMENT_RECYCLE(ELogFormatter) { destroyLogFormatter(object); }
ELOG_IMPLEMENT_RECYCLE(ELogFilter) { destroyFilter(object); }
ELOG_IMPLEMENT_RECYCLE(ELogFlushPolicy) { destroyFlushPolicy(object); }

void retireLogTargetFormatter(ELogFormatter* logFormatter) {
    ELOG_SCOPED_EPOCH(sLogTargetGC, sLogTargetEpoch);
    ELOG_RETIRE(sLogTargetGC, ELogFormatter, logFormatter, ELOG_CURRENT_EPOCH);
}

void retireLogTargetFilter(ELogFilter* logFilter) {
    ELOG_SCOPED_EPOCH(sLogTargetGC, sLogTargetEpoch);
    ELOG_RETIRE(sLogTargetGC, ELogFilter, logFilter, ELOG_CURRENT_EPOCH);
}

void retireLogTargetFlushPolicy(ELogFlushPolicy* flushPolicy) {
    ELOG_SCOPED_EPOCH(sLogTargetGC, sLogTargetEpoch);
    ELOG_RETIRE(sLogTargetGC, ELogFlushPolicy, flushPolicy, ELOG_CURRENT_EPOCH);
}

ELogGC* getLogTargetGC() { return sLogTargetGC; }

std::atomic<uint64_t>& getLogTargetEpoch() { return sLogTargetEpoch; }

#endif

#ifdef ELOG_ENABLE_DYNAMIC_CONFIG
ELogTargetId addLogTarget(ELogTarget* logTarget) {
    ELOG_REPORT_TRACE("Adding log target: %s", logTarget->getName());

    // NOTE: we must start the log target early because of statistics dependency (if started after
    // adding to array, then any ELOG_REPORT_XXX in between would trigger logging for the new target
    // before the statistics object was created) but some log targets require they have an id
    // allocated early, before start is called (e.g. Grafana), so that they can set up a debug
    // logger that does not send logs to itself

    // find vacant slot and reserve it for the log target
    ELogTargetId logTargetId = ELOG_INVALID_TARGET_ID;
    for (uint32_t i = 0; i < sLogTargets.size(); ++i) {
        ELogTarget* target = sLogTargets[i].m_atomicValue.load(std::memory_order_relaxed);
        if (target == nullptr && sLogTargets[i].m_atomicValue.compare_exchange_strong(
                                     target, ELOG_TARGET_RESERVED, std::memory_order_seq_cst)) {
            ELOG_REPORT_TRACE("Reserved slot %u to log target %s", i, logTarget->getName());
            logTargetId = i;
            break;
        }
    }

    // check if found
    if (logTargetId == ELOG_INVALID_TARGET_ID) {
        return ELOG_INVALID_TARGET_ID;
    }

    // set target id and start it
    logTarget->setId(logTargetId);

    // start target id
    if (!logTarget->start()) {
        ELOG_REPORT_ERROR("Failed to start log target %s", logTarget->getName());
        logTarget->setId(ELOG_INVALID_TARGET_ID);
        sLogTargets[logTargetId].m_atomicValue.store(nullptr, std::memory_order_release);
        return ELOG_INVALID_TARGET_ID;
    }

    // NOTE: we must increment epoch before putting the log target in the global array, since we are
    // about to write messages into it (and a concurrent remove may destroy it)
    ELOG_SCOPED_EPOCH(sLogTargetGC, sLogTargetEpoch);

    // now we can replace the reserved pointer to the real pointer
    sLogTargets[logTargetId].m_atomicValue.store(logTarget, std::memory_order_release);

    // write accumulated log messages if there are such
    getPreInitLoggerRef().writeAccumulatedLogMessages(logTarget);
    return logTargetId;
}
#else
ELogTargetId addLogTarget(ELogTarget* logTarget) {
    // TODO: should we guard against duplicate names (they are used in search by name and in log
    // affinity mask building) - this might require API change (returning at least bool)
    ELOG_REPORT_TRACE("Adding log target: %s", logTarget->getName());

    // NOTE: we must start the log target early because of statistics dependency (if started after
    // adding to array, then any ELOG_REPORT_XXX in between would trigger logging for the new target
    // before the statistics object was created) but some log targets require they have an id
    // allocated early, before start is called (e.g. Grafana), so that they can set up a debug
    // logger that does not send logs to itself

    // TODO: this is too sensitive, it is better to check whether the statistics object exists and
    // get rid of m_enableStats altogether

    // find vacant slot and reserve it for the log target
    ELogTargetId logTargetId = ELOG_INVALID_TARGET_ID;
    for (uint32_t i = 0; i < sLogTargets.size(); ++i) {
        if (sLogTargets[i] == nullptr) {
            sLogTargets[i] = ELOG_TARGET_RESERVED;
            ELOG_REPORT_TRACE("Reserved slot %u to log target %s", i, logTarget->getName());
            logTargetId = i;
            break;
        }
    }

    // otherwise add a new slot
    if (logTargetId == ELOG_INVALID_TARGET_ID) {
        if (sLogTargets.size() == ELOG_MAX_TARGET_COUNT) {
            ELOG_REPORT_ERROR("Cannot add log target, reached hard limit of log targets %u",
                              ELOG_MAX_TARGET_COUNT);
            return ELOG_INVALID_TARGET_ID;
        }
        logTargetId = (ELogTargetId)sLogTargets.size();
        sLogTargets.push_back(logTarget);
        ELOG_REPORT_TRACE("Added log target %s with id %u", logTarget->getName(), logTargetId);
    }

    // set target id and start it
    logTarget->setId(logTargetId);

    // start target id
    if (!logTarget->start()) {
        ELOG_REPORT_ERROR("Failed to start log target %s", logTarget->getName());
        logTarget->setId(ELOG_INVALID_TARGET_ID);
        sLogTargets[logTargetId] = nullptr;
        return ELOG_INVALID_TARGET_ID;
    }

    // now we can replace the reserved pointer to the real pointer
    sLogTargets[logTargetId] = logTarget;

    // write accumulated log messages if there are such
    getPreInitLoggerRef().writeAccumulatedLogMessages(logTarget);
    return logTargetId;
}
#endif

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
                                ELogFormatter* logFormatter /* = nullptr */,
                                ELogFlushPolicy* flushPolicy /* = nullptr */) {
    return attachLogFileTarget(stderr, false, 0, false, false, logLevel, flushPolicy, logFilter,
                               logFormatter);
}

ELogTargetId addStdOutLogTarget(ELogLevel logLevel /* = ELEVEL_INFO */,
                                ELogFilter* logFilter /* = nullptr */,
                                ELogFormatter* logFormatter /* = nullptr */,
                                ELogFlushPolicy* flushPolicy /* = nullptr */) {
    return attachLogFileTarget(stdout, false, 0, false, false, logLevel, flushPolicy, logFilter,
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

#ifdef ELOG_ENABLE_DYNAMIC_CONFIG
    // NOTE: we must increment epoch before getting the log target and using it, to guard against
    // concurrent remove
    ELOG_SCOPED_EPOCH(sLogTargetGC, sLogTargetEpoch);
#endif

    // now get the log target
    ELogTarget* logTarget = getLogTarget(id);
    if (logTarget == nullptr) {
        ELOG_REPORT_ERROR("Internal error while adding tracer, log target by id %u not found", id);
        return false;
    }

    // define log source
    ELogSource* logSource = defineLogSource(sourceName, true);
    if (logSource == nullptr) {
        ELOG_REPORT_ERROR("Failed to define tracer %s log source by name %s", targetName,
                          sourceName);
        return false;
    }

    // bind log source to target using affinity mask
    logSource->pairWithLogTarget(logTarget);
    return id;
}

ELogTarget* getLogTarget(ELogTargetId targetId) {
    if (targetId >= sLogTargets.size()) {
        return nullptr;
    }
#ifdef ELOG_ENABLE_DYNAMIC_CONFIG
    return sLogTargets[targetId].m_atomicValue.load(std::memory_order_relaxed);
#else
    return sLogTargets[targetId];
#endif
}

#ifdef ELOG_ENABLE_DYNAMIC_CONFIG
ELogTarget* getLogTarget(const char* logTargetName) {
    for (ELogAtomic<ELogTarget*>& logTargetRef : sLogTargets) {
        ELogTarget* logTarget = logTargetRef.m_atomicValue.load(std::memory_order_relaxed);
        if (logTarget != nullptr) {
            if (strcmp(logTarget->getName(), logTargetName) == 0) {
                return logTarget;
            }
        }
    }
    return nullptr;
}
#else
ELogTarget* getLogTarget(const char* logTargetName) {
    for (ELogTarget* logTarget : sLogTargets) {
        if (strcmp(logTarget->getName(), logTargetName) == 0) {
            return logTarget;
        }
    }
    return nullptr;
}
#endif

#ifdef ELOG_ENABLE_DYNAMIC_CONFIG
ELogTarget* acquireLogTarget(ELogTargetId targetId, uint64_t& epoch) {
    if (targetId > sLogTargets.size()) {
        ELOG_REPORT_ERROR("Cannot get log target by id %u, id out of range", targetId);
        return nullptr;
    }

    // increment epoch before checking pointer
    epoch = sLogTargetEpoch.fetch_add(1, std::memory_order_acquire);
    sLogTargetGC->beginEpoch(epoch);

    ELogTarget* logTarget = sLogTargets[targetId].m_atomicValue.load(std::memory_order_relaxed);
    return logTarget;
}

ELogTarget* acquireLogTarget(const char* logTargetName, uint64_t& epoch) {
    // increment epoch before checking pointers
    epoch = sLogTargetEpoch.fetch_add(1, std::memory_order_acquire);
    sLogTargetGC->beginEpoch(epoch);

    for (ELogAtomic<ELogTarget*>& logTargetRef : sLogTargets) {
        ELogTarget* logTarget = logTargetRef.m_atomicValue.load(std::memory_order_relaxed);
        if (logTarget != nullptr) {
            if (strcmp(logTarget->getName(), logTargetName) == 0) {
                return logTarget;
            }
        }
    }

    return nullptr;
}

void releaseLogTarget(uint64_t epoch) { sLogTargetGC->endEpoch(epoch); }
#endif

#ifdef ELOG_ENABLE_DYNAMIC_CONFIG
ELogTargetId getLogTargetId(const char* logTargetName) {
    // NOTE: we must increment epoch before accessing log targets, to guard against concurrent
    // remove
    ELOG_SCOPED_EPOCH(sLogTargetGC, sLogTargetEpoch);

    for (uint32_t logTargetId = 0; logTargetId < sLogTargets.size(); ++logTargetId) {
        ELogAtomic<ELogTarget*>& logTargetRef = sLogTargets[logTargetId];
        ELogTarget* logTarget = logTargetRef.m_atomicValue.load(std::memory_order_relaxed);
        if (logTarget != nullptr) {
            if (strcmp(logTarget->getName(), logTargetName) == 0) {
                return logTargetId;
            }
        }
    }
    return ELOG_INVALID_TARGET_ID;
}
#else
ELogTargetId getLogTargetId(const char* logTargetName) {
    for (uint32_t logTargetId = 0; logTargetId < sLogTargets.size(); ++logTargetId) {
        ELogTarget* logTarget = sLogTargets[logTargetId];
        if (strcmp(logTarget->getName(), logTargetName) == 0) {
            return logTargetId;
        }
    }
    return ELOG_INVALID_TARGET_ID;
}
#endif

#ifndef ELOG_ENABLE_DYNAMIC_CONFIG
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
#endif

#ifdef ELOG_ENABLE_DYNAMIC_CONFIG

// provide specialization for log target
ELOG_IMPLEMENT_RECYCLE(ELogTarget) { object->destroy(); }

inline bool retireLogTarget(ELogTarget* logTarget, uint64_t epoch) {
    ELogManagedObjectWrapper<ELogTarget>* managedLogTarget =
        new (std::nothrow) ELogManagedObjectWrapper<ELogTarget>(logTarget);
    if (managedLogTarget == nullptr) {
        ELOG_REPORT_ERROR(
            "Cannot retire log target for asynchronous reclamation, out of memory (memory leak "
            "inevitable)");
        return false;
    }
    return sLogTargetGC->retire(managedLogTarget, epoch);
}

bool removeLogTarget(ELogTargetId targetId) {
    // be careful, if this is the last log target, we must put back stderr
    if (targetId >= sLogTargets.size()) {
        ELOG_REPORT_ERROR("Cannot remove log target %u, id out of range", targetId);
        return false;
    }

    ELogTarget* logTarget = sLogTargets[targetId].m_atomicValue.load(std::memory_order_acquire);
    if (logTarget == nullptr) {
        ELOG_REPORT_ERROR("Cannot remove log target %u, not found", targetId);
        return false;
    }

    // in order to avoid race with other who try to remove the log target we first do a CAS
    if (!sLogTargets[targetId].m_atomicValue.compare_exchange_strong(logTarget, nullptr,
                                                                     std::memory_order_seq_cst)) {
        // we consider this an error because it indicates the user is doing something wrong
        // two threads should not be trying to remove the same log target
        ELOG_REPORT_ERROR("Cannot remove log target %u, concurrent modification", targetId);
        return false;
    }

    // now we can stop the log target
    // NOTE: we cannot shrink the vector because that will change log target indices
    ELOG_REPORT_TRACE("Stopping log target %s at %p", logTarget->getName(), logTarget);
    logTarget->stop();

    // NOTE: the epoch must be incremented only after the pointer was detached
    ELOG_REPORT_TRACE("Retiring log target %s at %p for later reclamation", logTarget->getName(),
                      logTarget);
    ELOG_SCOPED_EPOCH(sLogTargetGC, sLogTargetEpoch)
    return retireLogTarget(logTarget, ELOG_CURRENT_EPOCH);
}
#else
bool removeLogTarget(ELogTargetId targetId) {
    // be careful, if this is the last log target, we must put back stderr
    if (targetId >= sLogTargets.size()) {
        ELOG_REPORT_ERROR("Cannot remove log target %u, id out of range", targetId);
        return false;
    }

    if (sLogTargets[targetId] == nullptr) {
        ELOG_REPORT_ERROR("Cannot remove log target %u, not found", targetId);
        return false;
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
    return true;
}
#endif

#ifdef ELOG_ENABLE_DYNAMIC_CONFIG
void clearAllLogTargets() {
    // NOTE: since log target may have indirect dependencies (e.g. one log target, while writing a
    // log message, issues another log message which is dispatched to all other log targets), we
    // first stop all targets and then delete them, but this requires log target to be able to
    // reject log messages after stop() was called.

    // NOTE: in order to avoid race conditions, we first detach all log targets, then stop them,
    // then retire to GC
    std::vector<ELogTarget*> removedLogTargets;

    for (ELogAtomic<ELogTarget*>& logTargetRef : sLogTargets) {
        ELogTarget* logTarget = logTargetRef.m_atomicValue.load(std::memory_order_acquire);
        if (logTarget != nullptr && logTargetRef.m_atomicValue.compare_exchange_strong(
                                        logTarget, nullptr, std::memory_order_seq_cst)) {
            if (isTerminating() || (logTarget != nullptr && !logTarget->isSystemTarget())) {
                removedLogTargets.push_back(logTarget);
            }
        }
    }

    // NOTE: the epoch must be incremented only after each pointer was detached
    ELOG_SCOPED_EPOCH(sLogTargetGC, sLogTargetEpoch);

    // now we can stop them one by one
    for (ELogTarget* logTarget : removedLogTargets) {
        logTarget->stop();
    }

    // and finally, retire them one by one
    for (ELogTarget* logTarget : removedLogTargets) {
        retireLogTarget(logTarget, ELOG_CURRENT_EPOCH);
    }
}
#else
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
#endif

bool removeLogTarget(ELogTarget* target) { return removeLogTarget(target->getId()); }

#ifdef ELOG_ENABLE_DYNAMIC_CONFIG
void resetThreadStatCounters(uint64_t slotId) {
    // NOTE: we must increment epoch before accessing log targets, to guard against concurrent
    // remove
    ELOG_SCOPED_EPOCH(sLogTargetGC, sLogTargetEpoch);

    for (ELogAtomic<ELogTarget*>& logTargetRef : sLogTargets) {
        ELogTarget* logTarget = logTargetRef.m_atomicValue.load(std::memory_order_acquire);
        if (logTarget != nullptr) {
            ELogStats* stats = logTarget->getStats();
            if (stats != nullptr) {
                stats->resetThreadCounters(slotId);
            }
        }
    }
}
#else
void resetThreadStatCounters(uint64_t slotId) {
    for (ELogTargetId logTargetId = 0; logTargetId < sLogTargets.size(); ++logTargetId) {
        ELogTarget* logTarget = sLogTargets[logTargetId];
        ELogStats* stats = logTarget->getStats();
        if (stats != nullptr) {
            stats->resetThreadCounters(slotId);
        }
    }
}
#endif

#ifdef ELOG_ENABLE_DYNAMIC_CONFIG
bool logMsgTarget(const ELogRecord& logRecord, ELogTargetAffinityMask logTargetAffinityMask) {
    // NOTE: we must increment epoch before accessing log targets, to guard against concurrent
    // remove
    ELOG_SCOPED_EPOCH(sLogTargetGC, sLogTargetEpoch);

    bool logged = false;
    for (ELogTargetId logTargetId = 0; logTargetId < sLogTargets.size(); ++logTargetId) {
        ELogTarget* logTarget =
            sLogTargets[logTargetId].m_atomicValue.load(std::memory_order_relaxed);
        // NOTE: we may encounter a reserved entry (see addLogTarget above)
        if (logTarget == nullptr || logTarget == ELOG_TARGET_RESERVED) {
            continue;
        }
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

    return logged;
}
#else
bool logMsgTarget(const ELogRecord& logRecord, ELogTargetAffinityMask logTargetAffinityMask) {
    bool logged = false;
    for (ELogTargetId logTargetId = 0; logTargetId < sLogTargets.size(); ++logTargetId) {
        ELogTarget* logTarget = sLogTargets[logTargetId];
        // NOTE: we may encounter a reserved entry (see addLogTarget above)
        if (logTarget == nullptr || logTarget == ELOG_TARGET_RESERVED) {
            continue;
        }
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

    return logged;
}
#endif

}  // namespace elog