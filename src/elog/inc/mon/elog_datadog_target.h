#ifndef __ELOG_DATADOG_TARGET_H__
#define __ELOG_DATADOG_TARGET_H__

#include "elog_def.h"

#ifdef ELOG_ENABLE_DATADOG_CONNECTOR

#ifndef ELOG_ENABLE_HTTP
#ifdef ELOG_MSVC
#error "Invalid configuration, Datadog connector requires HTTP client"
#else
#pragma GCC diagnostic error "Invalid configuration, Datadog connector requires HTTP client"
#endif
#endif

#ifndef ELOG_ENABLE_JSON
#ifdef ELOG_MSVC
#error "Invalid configuration, Datadog connector requires JSON"
#else
#pragma GCC diagnostic error "Invalid configuration, Datadog connector requires JSON"
#endif
#endif

#include <nlohmann/json.hpp>

#include "elog_http_client.h"
#include "elog_mon_target.h"
#include "elog_props_formatter.h"

namespace elog {

class ELOG_API ELogDatadogTarget : public ELogMonTarget, public ELogHttpClientAssistant {
public:
    ELogDatadogTarget(const char* serverAddress, const char* apiKey, const ELogHttpConfig& config,
                      const char* source = "", const char* service = "", const char* tags = "",
                      bool stackTrace = false, bool compress = false);

    ELogDatadogTarget(const ELogDatadogTarget&) = delete;
    ELogDatadogTarget(ELogDatadogTarget&&) = delete;
    ELogDatadogTarget& operator=(const ELogDatadogTarget&) = delete;

    ELOG_DECLARE_LOG_TARGET(ELogDatadogTarget)

    /** @brief Embed headers in outgoing HTTP message. */
    void embedHeaders(httplib::Headers& headers) final;

    /**
     * @brief Handles HTTP POST result.
     * @param result The result to examine.
     * @return true If the result is regarded as success.
     * @return false If the result is regarded as failure, in which case the HTTP message will be
     * stored in a backlog for future attempt to resend to the server. Pay attention that when some
     * errors occur it does not make sense to resend, since the same error would occur again (e.g.
     * invalid payload, wrong endpoint name, etc.).
     */
    bool handleResult(const httplib::Result& result) final;

protected:
    /** @brief Order the log target to start (required for threaded targets). */
    bool startLogTarget() override;

    /** @brief Order the log target to stop (required for threaded targets). */
    bool stopLogTarget() override;

    /**
     * @brief Order the log target to write a log record (thread-safe).
     * @return The number of bytes written to log.
     */
    uint32_t writeLogRecord(const ELogRecord& logRecord) override;

    /** @brief Order the log target to flush. */
    bool flushLogTarget() override;

private:
    std::string m_apiKey;
    std::string m_source;
    std::string m_service;
    std::string m_tags;
    bool m_stackTrace;
    bool m_compress;

    ELogHttpClient m_client;
    nlohmann::json m_logItemArray;
    ELogPropsFormatter* m_tagsFormatter;

    inline bool parseTags(const std::string& tags) { return m_tagsFormatter->parseProps(tags); }

    inline const std::vector<std::string>& getTagNames() const {
        return m_tagsFormatter->getPropNames();
    }

    inline void fillInTags(const elog::ELogRecord& logRecord, elog::ELogFieldReceptor* receptor) {
        m_tagsFormatter->fillInProps(logRecord, receptor);
    }

    bool prepareTagsString(const std::vector<std::string>& propNames,
                           const std::vector<std::string>& propValues, std::string& tags);
};

}  // namespace elog

#endif  // ELOG_ENABLE_SENTRY_CONNECTOR

#endif  // __ELOG_DATADOG_TARGET_H__