#include "elog_sentry_target.h"

#ifdef ELOG_ENABLE_SENTRY_CONNECTOR

#include <sentry.h>

#include "elog_common.h"
#include "elog_error.h"
#include "elog_field_selector_internal.h"
#include "elog_logger.h"
#include "elog_system.h"

#ifdef ELOG_ENABLE_STACK_TRACE
#include "dbg_stack_trace.h"
#include "dbg_util.h"
#include "os_module_manager.h"
#endif

namespace elog {

// TODO: understand how to work with Sentry transactions

// TODO: if there is a SENTRY_DSN env var then use it (override config, also allows empty config)

// TODO: checkout the beta logging interface

// TODO: learn how to use envelopes and transport

// TODO: file requires reorganizing

static ELogLogger* sSentryLogger = nullptr;

#ifdef ELOG_ENABLE_STACK_TRACE
static void* sELogBaseAddress = nullptr;
static void* sDbgUtilBaseAddress = nullptr;
#endif

inline sentry_level_e elogLevelToSentryLevel(ELogLevel logLevel) {
    switch (logLevel) {
        case ELEVEL_FATAL:
            return SENTRY_LEVEL_FATAL;
        case ELEVEL_ERROR:
            return SENTRY_LEVEL_ERROR;
        case ELEVEL_WARN:
            return SENTRY_LEVEL_WARNING;
        case ELEVEL_NOTICE:
        case ELEVEL_INFO:
            return SENTRY_LEVEL_INFO;
        default:
            return SENTRY_LEVEL_DEBUG;
    }
}

inline ELogLevel sentryLogLevelToELog(sentry_level_t logLevel) {
    switch (logLevel) {
        case SENTRY_LEVEL_FATAL:
            return ELEVEL_FATAL;
        case SENTRY_LEVEL_ERROR:
            return ELEVEL_ERROR;
        case SENTRY_LEVEL_WARNING:
            return ELEVEL_WARN;
        case SENTRY_LEVEL_INFO:
            return ELEVEL_INFO;
        case SENTRY_LEVEL_DEBUG:
            return ELEVEL_DEBUG;
        default:
            return ELEVEL_INFO;
    }
}

static void sentryLoggerFunc(sentry_level_t level, const char* message, va_list args,
                             void* userdata);

class ELogSentryContextReceptor : public ELogFieldReceptor {
public:
    ELogSentryContextReceptor() { m_context = sentry_value_new_object(); }
    ~ELogSentryContextReceptor() final {}

    /** @brief Receives a string log record field. */
    void receiveStringField(uint32_t typeId, const char* field, const ELogFieldSpec& fieldSpec,
                            size_t length) {
        sentry_value_set_by_key(m_context, fieldSpec.m_name.c_str(),
                                sentry_value_new_string(field));
    }

    /** @brief Receives an integer log record field. */
    void receiveIntField(uint32_t typeId, uint64_t field, const ELogFieldSpec& fieldSpec) final {
        sentry_value_set_by_key(m_context, fieldSpec.m_name.c_str(),
                                sentry_value_new_int32((int32_t)field));
    }

    /** @brief Receives a time log record field. */
    void receiveTimeField(uint32_t typeId, const ELogTime& logTime, const char* timeStr,
                          const ELogFieldSpec& fieldSpec) final {
        // time cannot be part of context
    }

    /** @brief Receives a log level log record field. */
    void receiveLogLevelField(uint32_t typeId, ELogLevel logLevel,
                              const ELogFieldSpec& fieldSpec) final {
        // time cannot be part of context
    }

    void applyContext(const char* name) { sentry_set_context(name, m_context); }

private:
    sentry_value_t m_context;
};

class ELogSentryTagsReceptor : public ELogFieldReceptor {
public:
    ELogSentryTagsReceptor() {}
    ~ELogSentryTagsReceptor() final {}

    /** @brief Receives a string log record field. */
    void receiveStringField(uint32_t typeId, const char* field, const ELogFieldSpec& fieldSpec,
                            size_t length) {
        m_tagValues.push_back(field);
    }

    /** @brief Receives an integer log record field. */
    void receiveIntField(uint32_t typeId, uint64_t field, const ELogFieldSpec& fieldSpec) final {
        m_tagValues.push_back(std::to_string(field));
    }

    /** @brief Receives a time log record field. */
    void receiveTimeField(uint32_t typeId, const ELogTime& logTime, const char* timeStr,
                          const ELogFieldSpec& fieldSpec) final {
        m_tagValues.push_back(timeStr);
    }

    /** @brief Receives a log level log record field. */
    void receiveLogLevelField(uint32_t typeId, ELogLevel logLevel,
                              const ELogFieldSpec& fieldSpec) final {
        const char* logLevelStr = elogLevelToStr(logLevel);
        m_tagValues.push_back(logLevelStr);
    }

    bool applyTags(const std::vector<std::string>& tagNames, uint32_t& bytesWritten) {
        if (m_tagValues.size() != tagNames.size()) {
            ELOG_REPORT_ERROR("Mismatching tag names a values (%u names, %u values)",
                              tagNames.size(), m_tagValues.size());
            return false;
        }
        for (uint32_t i = 0; i < m_tagValues.size(); ++i) {
            sentry_set_tag(tagNames[i].c_str(), m_tagValues[i].c_str());
            bytesWritten += tagNames[i].length() + m_tagValues[i].length();
        }
        return true;
    }

private:
    std::vector<std::string> m_tagValues;
};

bool ELogSentryTarget::startLogTarget() {
    // parse context and tags if any
    if (!m_params.m_context.empty()) {
        if (!m_contextFormatter.initialize(m_params.m_context.c_str())) {
            ELOG_REPORT_ERROR("Invalid context specification for Sentry log target: %s",
                              m_params.m_context.c_str());
            return false;
        }
    }

    // we fabricate a log record so we can get the context fields resolved
    // NOTE: context is set only once and is shared among all events
    ELogRecord dummy = {};
    ELogSentryContextReceptor contextReceptor;
    m_contextFormatter.fillInProps(dummy, &contextReceptor);
    contextReceptor.applyContext(m_params.m_contextTitle.c_str());

    if (!m_params.m_tags.empty()) {
        if (!m_tagsFormatter.initialize(m_params.m_tags.c_str())) {
            ELOG_REPORT_ERROR("Invalid tags specification for Sentry log target: %s",
                              m_params.m_tags.c_str());
            return false;
        }
    }

    // set options
    sentry_options_t* options = sentry_options_new();
    sentry_options_set_dsn(options, m_params.m_dsn.c_str());
    if (!m_params.m_dbPath.empty()) {
        sentry_options_set_database_path(options, m_params.m_dbPath.c_str());
    }
    if (!m_params.m_releaseName.empty()) {
        sentry_options_set_release(options, m_params.m_releaseName.c_str());
    }
    if (!m_params.m_env.empty()) {
        sentry_options_set_environment(options, m_params.m_env.c_str());
    }
    if (!m_params.m_dist.empty()) {
        sentry_options_set_dist(options, m_params.m_dist.c_str());
    }
    if (!m_params.m_caCertsPath.empty()) {
        sentry_options_set_ca_certs(options, m_params.m_caCertsPath.c_str());
    }
    if (!m_params.m_proxy.empty()) {
        sentry_options_set_proxy(options, m_params.m_proxy.c_str());
    }
    // if user built Sentry with launch-pad backend, then he should also provide handler path, so we
    // should allow it from configuration (optional)
    if (!m_params.m_handlerPath.empty()) {
        sentry_options_set_handler_path(options, m_params.m_handlerPath.c_str());
    }

    sentry_options_set_shutdown_timeout(options, m_params.m_shutdownTimeoutMillis);
    sentry_options_set_debug(options, m_params.m_debug ? 1 : 0);

    // finally configure sentry logger (only if debug is set)
    if (m_params.m_debug) {
        ELogSource* logSource = ELogSystem::defineLogSource("elog.sentry", true);
        if (logSource != nullptr) {
            // make sure we do not enter infinite loop, so we make sentry log source writes only to
            // stderr, this requires stderr to be defined BEFORE sentry log target
            ELogTargetAffinityMask mask = 0;
            ELogTargetId logTargetId = ELogSystem::getLogTargetId("stderr");
            if (logTargetId != ELOG_INVALID_TARGET_ID) {
                ELOG_ADD_TARGET_AFFINITY_MASK(mask, logTargetId);
                logSource->setLogTargetAffinity(mask);
            } else {
                ELOG_REPORT_WARN(
                    "Could not restrict sentry log source to stderr (stderr log target not found)");
            }
            sSentryLogger = logSource->createSharedLogger();
        } else {
            ELOG_REPORT_WARN("Sentry logger could not be set up, failed to define log source");
        }
        sentry_options_set_logger(options, sentryLoggerFunc, nullptr);

        // allow user to control log level
        if (!m_params.m_loggerLevel.empty()) {
            ELogLevel level = ELEVEL_INFO;
            if (!elogLevelFromStr(m_params.m_loggerLevel.c_str(), level)) {
                ELOG_REPORT_WARN("Invalid logger level '%s' for Sentry logger, using INFO instead");
                level = ELEVEL_INFO;
            }
            sentry_options_set_logger_level(options, elogLevelToSentryLevel(level));
        }
    }
    sentry_init(options);

#ifdef ELOG_ENABLE_STACK_TRACE
    dbgutil::OsModuleInfo modInfo;
#ifdef ELOG_WINDOWS
    const char* elogModName = "elog.dll";
    const char* dbgutilModName = "dbgutil.dll";
#else
    const char* elogModName = "elog.so";
    const char* dbgutilModName = "dbgutil.so";
#endif
    dbgutil::DbgUtilErr rc =
        dbgutil::getModuleManager()->getModuleByName(elogModName, modInfo, true);
    if (rc == DBGUTIL_ERR_OK) {
        sELogBaseAddress = modInfo.m_loadAddress;
    }
    rc = dbgutil::getModuleManager()->getModuleByName(dbgutilModName, modInfo);
    if (rc == DBGUTIL_ERR_OK) {
        sDbgUtilBaseAddress = modInfo.m_loadAddress;
    }
#endif

    return true;
}

bool ELogSentryTarget::stopLogTarget() {
    // NOTE: we get a deadlock here because sentry_close() outputs a debug message, but stderr log
    // target may have already been deleted by now.
    // first we note that setting sSentryLogger to null before calling sentry_close() is not enough,
    // since during the time window since stderr had been deleted, until the time we got here, there
    // may be more messages issued by sentry logger, which will lead to crash/deadlock

    // we can handle this in several ways:
    // - the best way would be to get notification "log target stopped", so when stderr goes does,
    // and before it gets deleted, we can nullify the sentry logger (and then write directly to
    // stderr or whatever)
    // - we can also register a "log target started", so we can restrict affinity to stderr log
    // target when it gets added, and also register for its removal event
    // - we can avoid restricting ourselves to stderr log target altogether, and instead, we just
    // make sure we don't send logs to ourselves. for this to happen we must get the log target id
    // of the sentry log target, but during start it is not present yet. If we modify log target to
    // first grab a slot (lock free), and then call start (and if failed release slot), then this
    // would work, we only need to put the target id in the target, so we don't need to guess what
    // log target name the user used
    // the last solution is the best, since it does not require dependency on a specific log target
    // (what if it is not defined, or defined not early enough?), but it requires more development
    // effort, which is not that much

    // for now we will just set it to null
    sSentryLogger = nullptr;
    sentry_close();
    return true;
}

uint32_t ELogSentryTarget::writeLogRecord(const ELogRecord& logRecord) {
    std::string logMsg;
    formatLogMsg(logRecord, logMsg);
    sentry_value_t evt = sentry_value_new_message_event(
        /*   level */ elogLevelToSentryLevel(logRecord.m_logLevel),
        /*  logger */ logRecord.m_logger->getLogSource()->getQualifiedName(),
        /* message */ logMsg.c_str());

    // append additional event as tags if configured to do so
    uint32_t bytesWritten = 0;
    if (!m_params.m_tags.empty()) {
        ELogSentryTagsReceptor receptor;
        m_tagsFormatter.fillInProps(logRecord, &receptor);
        receptor.applyTags(m_tagsFormatter.getPropNames(), bytesWritten);
    }

    // append current thread attributes
    sentry_value_t thd = sentry_value_new_thread(getCurrentThreadId(), nullptr);
    sentry_value_set_by_key(thd, "id", sentry_value_new_int32(getCurrentThreadId()));
    sentry_value_set_by_key(thd, "current", sentry_value_new_bool(true));
    const char* currThreadName = getCurrentThreadNameField();
    if (currThreadName != nullptr && *currThreadName != 0) {
        sentry_value_set_by_key(thd, "name", sentry_value_new_string(currThreadName));
    }

    // append additional stack trace if configured to do so
    // unlike Sentry, we can report fully resolved frames
    if (m_params.m_stackTrace) {
#ifdef ELOG_ENABLE_STACK_TRACE
        // since we are able to provide full stack trace information, we bypass the high-level API
        // sentry_value_set_stacktrace(), and instead set the attributes manually

        // get the stack trace and fill in frames information
        dbgutil::StackTrace stackTrace;
        dbgutil::getStackTrace(stackTrace);
        sentry_value_t frames = sentry_value_new_list();

        // traverse in reverse order due to sentry requirement (first frame is oldest)
        for (dbgutil::StackTrace::reverse_iterator itr = stackTrace.rbegin();
             itr != stackTrace.rend(); ++itr) {
            dbgutil::StackEntry& stackEntry = *itr;

            // skip frames with elog or dbgutil module, so that stack is cleaner
            if (stackEntry.m_entryInfo.m_moduleBaseAddress == sELogBaseAddress ||
                stackEntry.m_entryInfo.m_moduleBaseAddress == sDbgUtilBaseAddress) {
                continue;
            }

            // set frame address
            sentry_value_t frame = sentry_value_new_object();
            std::stringstream s;
            s << "0x" << std::hex << stackEntry.m_frameAddress;
            sentry_value_set_by_key(frame, "instruction_addr",
                                    sentry_value_new_string(s.str().c_str()));
            s.str(std::string());  // clear string stream

            // set image address
            s << "0x" << std::hex << stackEntry.m_entryInfo.m_moduleBaseAddress;
            sentry_value_set_by_key(frame, "image_addr", sentry_value_new_string(s.str().c_str()));

            // set image path
            sentry_value_set_by_key(
                frame, "package",
                sentry_value_new_string(stackEntry.m_entryInfo.m_moduleName.c_str()));

            // set file name
            sentry_value_set_by_key(
                frame, "filename",
                sentry_value_new_string(stackEntry.m_entryInfo.m_fileName.c_str()));

            // set function
            sentry_value_set_by_key(
                frame, "function",
                sentry_value_new_string(stackEntry.m_entryInfo.m_symbolName.c_str()));

            // set module
            sentry_value_set_by_key(
                frame, "module",
                sentry_value_new_string(stackEntry.m_entryInfo.m_moduleName.c_str()));

            // set line number
            sentry_value_set_by_key(frame, "lineno",
                                    sentry_value_new_int32(stackEntry.m_entryInfo.m_lineNumber));

            // set column number
            sentry_value_set_by_key(frame, "colno",
                                    sentry_value_new_int32(stackEntry.m_entryInfo.m_columnIndex));

            // add the frame to the frame list
            sentry_value_append(frames, frame);
        }

        // create a stack trace object and set the frames attribute
        sentry_value_t st = sentry_value_new_object();
        sentry_value_set_by_key(st, "frames", frames);
        sentry_value_set_by_key(thd, "stacktrace", st);
        // sentry_value_set_stacktrace(thd, &stackTrace[0], stackTrace.size());
#endif
    }
    sentry_event_add_thread(evt, thd);

    sentry_capture_event(evt);
    return bytesWritten + logMsg.length();
}

void ELogSentryTarget::flushLogTarget() {
    int res = sentry_flush(m_params.m_flushTimeoutMillis);
    if (res != 0) {
        ELOG_REPORT_TRACE("Failed to flush Sentry transport (timeout?)");
    }
}

void sentryLoggerFunc(sentry_level_t level, const char* message, va_list args, void* userdata) {
    if (sSentryLogger != nullptr) {
        sSentryLogger->logFormatV(sentryLogLevelToELog(level), "", 0, "", message, args);
    } else {
        vfprintf(stderr, message, args);
    }
}

}  // namespace elog

#endif  // ELOG_ENABLE_GRAFANA_CONNECTOR