#ifndef __ELOG_FORMATTER_H__
#define __ELOG_FORMATTER_H__

#include <string>
#include <vector>

#include "elog_def.h"
#include "elog_field_selector.h"
#include "elog_record.h"

namespace elog {

// the following special tokens can be sued in configuration:
// ${rid} ${time} ${host} ${user} ${pid} ${tid} ${src} ${msg}

// TODO: consider having a parent interface with no ctor params

/** @class Utility class for formatting log messages. */
class ELOG_API ELogFormatter {
public:
    ELogFormatter() {}
    virtual ~ELogFormatter();

    /**
     * @brief Initializes the log formatter.
     * @param logLineFormatSpec The log line format specification. The following special tokens are
     * interpreted as log record field references: ${rid} ${time} ${tid} ${src} ${msg}. The
     * following additional tokens are understood: ${host} for host name,
     * ${user} for logged in user, ${pid} for current process id, and ${mod} for module name. More
     * custom tokens can be added by deriving from @ref ELogFormatter and overriding the virtual
     * method @ref createFieldSelector().
     * @return true If the log line format specification was parsed successfully, otherwise false.
     */
    inline bool initialize(const char* logLineFormatSpec = "${time} ${level:6} [${tid}] ${msg}") {
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
     * @param justify Field justification value. Positive value represents left justification.
     * @return ELogFieldSelector* The resulting field selector or null if failed.
     */
    virtual ELogFieldSelector* createFieldSelector(const char* fieldName, int justify);

private:
    std::vector<ELogFieldSelector*> m_fieldSelectors;

    bool parseFormatSpec(const std::string& formatSpec);
};

}  // namespace elog

#endif  // __ELOG_FORMATTER_H__