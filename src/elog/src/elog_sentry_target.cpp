#include "elog_sentry_target.h"

#ifdef ELOG_ENABLE_SENTRY_CONNECTOR

#include <sentry.h>

#include "elog.h"
#include "elog_common.h"
#include "elog_field_selector_internal.h"
#include "elog_logger.h"
#include "elog_report.h"

#ifdef ELOG_ENABLE_STACK_TRACE
#include "elog_stack_trace.h"
#endif

namespace elog {

// TODO: understand how to work with Sentry transactions

// TODO: checkout the beta logging interface

// TODO: learn how to use envelopes and transport (so we can incorporate flush policy correctly)

static ELogLogger* sSentryLogger = nullptr;

#ifdef ELOG_ENABLE_STACK_TRACE
static bool buildStackTrace(sentry_value_t& st) {
    // since we are able to provide full stack trace information, we bypass the high-level API
    // sentry_value_set_stacktrace(), and instead set the attributes manually

    // get the stack trace and fill in frames information
    dbgutil::StackTrace stackTrace;
    if (!getStackTraceVector(stackTrace)) {
        return false;
    }
    sentry_value_t frames = sentry_value_new_list();

    // traverse in reverse order due to sentry requirement (first frame is oldest)
    for (dbgutil::StackTrace::reverse_iterator itr = stackTrace.rbegin(); itr != stackTrace.rend();
         ++itr) {
        dbgutil::StackEntry& stackEntry = *itr;

        // set frame address
        sentry_value_t frame = sentry_value_new_object();
        std::stringstream s;
#ifdef ELOG_MSVC
        s << "0x";
#endif
        s << std::hex << stackEntry.m_frameAddress;
        sentry_value_set_by_key(frame, "instruction_addr",
                                sentry_value_new_string(s.str().c_str()));
        s.str(std::string());  // clear string stream

        // set image address
#ifdef ELOG_MSVC
        s << "0x";
#endif
        s << std::hex << stackEntry.m_entryInfo.m_moduleBaseAddress;
        sentry_value_set_by_key(frame, "image_addr", sentry_value_new_string(s.str().c_str()));

        // set image path
        sentry_value_set_by_key(
            frame, "package", sentry_value_new_string(stackEntry.m_entryInfo.m_moduleName.c_str()));

        // set file name
        sentry_value_set_by_key(frame, "filename",
                                sentry_value_new_string(stackEntry.m_entryInfo.m_fileName.c_str()));

        // set function
        sentry_value_set_by_key(
            frame, "function",
            sentry_value_new_string(stackEntry.m_entryInfo.m_symbolName.c_str()));

        // set module
        sentry_value_set_by_key(
            frame, "module", sentry_value_new_string(stackEntry.m_entryInfo.m_moduleName.c_str()));

        // set line number
        sentry_value_set_by_key(
            frame, "lineno", sentry_value_new_int32((int32_t)stackEntry.m_entryInfo.m_lineNumber));

        // set column number
        sentry_value_set_by_key(
            frame, "colno", sentry_value_new_int32((int32_t)stackEntry.m_entryInfo.m_columnIndex));

        // add the frame to the frame list
        sentry_value_append(frames, frame);
    }

    // create a stack trace object and set the frames attribute
    st = sentry_value_new_object();
    sentry_value_set_by_key(st, "frames", frames);
    return true;
}
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

        case ELEVEL_TRACE:
        case ELEVEL_DEBUG:
        case ELEVEL_DIAG:
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

static void initSentryLogger(ELogTargetId sentryLogTargetId) {
    ELogSource* logSource = elog::defineLogSource("elog.sentry", true);
    if (logSource != nullptr) {
        // make sure we do not enter infinite loop, so we ensure sentry log source does NOT write to
        // the sentry log target
        ELogTargetAffinityMask mask = ELOG_ALL_TARGET_AFFINITY_MASK;
        ELOG_REMOVE_TARGET_AFFINITY_MASK(mask, sentryLogTargetId);
        logSource->setLogTargetAffinity(mask);
        sSentryLogger = logSource->createSharedLogger();
    } else {
        ELOG_REPORT_WARN("Sentry logger could not be set up, failed to define log source");
    }
}

static void sentryLoggerFunc(sentry_level_t level, const char* message, va_list args,
                             void* userdata);

// field receptor for setting context
class ELogSentryContextReceptor : public ELogFieldReceptor {
public:
    ELogSentryContextReceptor() { m_context = sentry_value_new_object(); }
    ELogSentryContextReceptor(const ELogSentryContextReceptor&) = delete;
    ELogSentryContextReceptor(ELogSentryContextReceptor&&) = delete;
    ELogSentryContextReceptor& operator=(const ELogSentryContextReceptor&) = delete;
    ~ELogSentryContextReceptor() final {}

    /** @brief Receives a string log record field. */
    void receiveStringField(uint32_t typeId, const char* field, const ELogFieldSpec& fieldSpec,
                            size_t length) final {
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
                          const ELogFieldSpec& fieldSpec, size_t length) final {
        // time cannot be part of context
    }

    /** @brief Receives a log level log record field. */
    void receiveLogLevelField(uint32_t typeId, ELogLevel logLevel,
                              const ELogFieldSpec& fieldSpec) final {
        // log level cannot be part of context
    }

    void applyContext(const char* name) { sentry_set_context(name, m_context); }

private:
    sentry_value_t m_context;
};

// field receptor for setting context
class ELogSentryTagsReceptor : public ELogFieldReceptor {
public:
    ELogSentryTagsReceptor() {}
    ELogSentryTagsReceptor(const ELogSentryTagsReceptor&) = delete;
    ELogSentryTagsReceptor(ELogSentryTagsReceptor&&) = delete;
    ELogSentryTagsReceptor& operator=(const ELogSentryTagsReceptor&) = delete;
    ~ELogSentryTagsReceptor() final {}

    /** @brief Receives a string log record field. */
    void receiveStringField(uint32_t typeId, const char* field, const ELogFieldSpec& fieldSpec,
                            size_t length) final {
        m_tagValues.push_back(field);
    }

    /** @brief Receives an integer log record field. */
    void receiveIntField(uint32_t typeId, uint64_t field, const ELogFieldSpec& fieldSpec) final {
        m_tagValues.push_back(std::to_string(field));
    }

    /** @brief Receives a time log record field. */
    void receiveTimeField(uint32_t typeId, const ELogTime& logTime, const char* timeStr,
                          const ELogFieldSpec& fieldSpec, size_t length) final {
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
            // NOTE: no chance of overflow here
            bytesWritten += (uint32_t)(tagNames[i].length() + m_tagValues[i].length());
        }
        return true;
    }

private:
    std::vector<std::string> m_tagValues;
};

bool ELogSentryTarget::startLogTarget() {
    // process context if any
    if (!m_params.m_context.empty()) {
        if (!m_contextFormatter.parseProps(m_params.m_context.c_str())) {
            ELOG_REPORT_ERROR("Invalid context specification for Sentry log target: %s",
                              m_params.m_context.c_str());
            return false;
        }
    }

    // process tags if any
    if (!m_params.m_tags.empty()) {
        if (!m_tagsFormatter.parseProps(m_params.m_tags.c_str())) {
            ELOG_REPORT_ERROR("Invalid tags specification for Sentry log target: %s",
                              m_params.m_tags.c_str());
            return false;
        }
    }

    // process attributes if any
    if (!m_params.m_attributes.empty()) {
        if (!m_tagsFormatter.parseProps(m_params.m_attributes.c_str())) {
            ELOG_REPORT_ERROR("Invalid attributes specification for Sentry log target: %s",
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
    // NOTE: on Windows, the vcpkg package manager builds sentry with crashpad, so this is required
    // TODO: add this to README
    if (!m_params.m_handlerPath.empty()) {
        sentry_options_set_handler_path(options, m_params.m_handlerPath.c_str());
    }

    sentry_options_set_shutdown_timeout(options, m_params.m_shutdownTimeoutMillis);
    sentry_options_set_debug(options, m_params.m_debug ? 1 : 0);

    // finally configure sentry logger (only if debug is set)
    if (m_params.m_debug) {
        initSentryLogger(getId());  // pass self id in order to filter out sentry log target
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
    // The solution will be implemented in two phases:
    // 1. non-thread-safe solution - get target id, add it to log target and start log target
    // 2. consider adding thread-safety to log target array, either lock-free or read-write lock
    //    this is not required here, but might be required if we consider external configuration
    //    changes that allow adding targets and configuring ELog from outside by external process

    // for now we will just set it to null
    sentry_close();
    sSentryLogger = nullptr;
    return true;
}

uint32_t ELogSentryTarget::writeLogRecord(const ELogRecord& logRecord) {
    std::string logMsg;
    formatLogMsg(logRecord, logMsg);
    sentry_value_t evt = sentry_value_new_message_event(
        /*   level */ elogLevelToSentryLevel(logRecord.m_logLevel),
        /*  logger */ logRecord.m_logger->getLogSource()->getQualifiedName(),
        /* message */ logMsg.c_str());

    // append additional event context if configured to do so
    if (!m_params.m_context.empty()) {
        ELogSentryContextReceptor contextReceptor;
        m_contextFormatter.fillInProps(logRecord, &contextReceptor);
        contextReceptor.applyContext(m_params.m_contextTitle.c_str());
    }

    // append additional event as tags if configured to do so
    uint32_t bytesWritten = 0;
    if (!m_params.m_tags.empty()) {
        ELogSentryTagsReceptor receptor;
        m_tagsFormatter.fillInProps(logRecord, &receptor);
        receptor.applyTags(m_tagsFormatter.getPropNames(), bytesWritten);
    }

    // append current thread attributes
    sentry_value_t thd = sentry_value_new_thread(getCurrentThreadId(), nullptr);
    sentry_value_set_by_key(thd, "id", sentry_value_new_int32((int32_t)getCurrentThreadId()));
    sentry_value_set_by_key(thd, "current", sentry_value_new_bool(true));
    const char* currThreadName = getThreadNameField(logRecord.m_threadId);
    if (currThreadName != nullptr && *currThreadName != 0) {
        sentry_value_set_by_key(thd, "name", sentry_value_new_string(currThreadName));
    }

    // append additional stack trace if configured to do so
    // unlike Sentry, we can report fully resolved frames
    if (m_params.m_stackTrace) {
#ifdef ELOG_ENABLE_STACK_TRACE
        // create a stack trace object and set the frames attribute
        sentry_value_t stackTrace;
        if (buildStackTrace(stackTrace)) {
            sentry_value_set_by_key(thd, "stacktrace", stackTrace);
        }
#endif
    }
    sentry_event_add_thread(evt, thd);

    // hand over the ready event to Sentry background thread
    sentry_capture_event(evt);
    // TODO: this number a bit misleading, since we may also have stack trace in payload
    // this will be more critical when using non-trivial flush policy
    return bytesWritten + (uint32_t)logMsg.length();

    // TODO: Currently the native SDK does not support yet new logs interface, when it does we will
    // send it as well
}

bool ELogSentryTarget::flushLogTarget() {
    int res = sentry_flush(m_params.m_flushTimeoutMillis);
    if (res != 0) {
        ELOG_REPORT_TRACE("Failed to flush Sentry transport (timeout?)");
        return false;
    }
    return true;
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