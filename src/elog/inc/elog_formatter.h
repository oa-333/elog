#ifndef __ELOG_FORMATTER_H__
#define __ELOG_FORMATTER_H__

#include <string>
#include <vector>

#include "elog_field_selector.h"
#include "elog_record.h"

namespace elog {

/** @def Flag specifying to include log record id. */
#define ELOG_FORMAT_RID

/** @def Flag specifying to include log time stamp. */
#define ELOG_FORMAT_TIME
/** @def Flag specifying to include log time stamp milliseconds fraction. */
#define ELOG_FORMAT_MSEC

/** @def Flag specifying to include host name/address. */
#define ELOG_FORMAT_HOST

/** @def Flag specifying to include process id. */
#define ELOG_FORMAT_PID

/** @def Flag specifying to include thread id. */
#define ELOG_FORMAT_TID
/** @def Flag specifying to include module name. */
#define ELOG_FORMAT_MODULE

// following special tokens can be sued in configuration:
// ${rid} ${time} ${host} ${user} ${pid} ${tid} ${src} ${msg}

// TODO: consider having a parent interface with no ctor params

/** @class Utility class for formatting log messages. */
class ELogFormatter {
public:
    ELogFormatter() {}
    virtual ~ELogFormatter();

    /**
     * @brief Initializes the log formatter.
     * @param logLineFormatSpec The log line format specification. The following special tokens are
     * interpreted as log record field references: ${rid} ${time} ${host} ${user} ${pid} ${tid}
     * ${src} ${msg}.
     * @return true If log line format specification was parsed successfully, otherwise false.
     */
    inline bool initialize(const char* logLineFormatSpec = "${time} ${tid} ${level} ${msg}") {
        return parseFormatSpec(logLineFormatSpec);
    }

    /**
     * @brief Formats a log message. The default log format is: TIME TID LEVEL MSG
     * @param logRecord The log record to format.
     * @param[out] logMsg The resulting formatted log message.
     */
    virtual void formatLogMsg(const ELogRecord& logRecord, std::string& logMsg);

protected:
    /**
     * @brief Create a field selector object by name (factory method). This is an extendible factory
     * method, and it can be extended to include new field selectors, even ones that extract data
     * from external systems.
     * @param fieldName The field name.
     * @return ELogFieldSelector* The resulting field selector or null if failed.
     */
    virtual ELogFieldSelector* createFieldSelector(const char* fieldName);

private:
    std::vector<ELogFieldSelector*> m_fieldSelectors;

    bool parseFormatSpec(const std::string& formatSpec);
};

}  // namespace elog

#endif  // __ELOG_FORMATTER_H__