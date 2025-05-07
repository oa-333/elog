#ifndef __ELOG_BASE_FORMATTER_H__
#define __ELOG_BASE_FORMATTER_H__

#include <string>
#include <vector>

#include "elog_def.h"
#include "elog_field_selector.h"
#include "elog_record.h"

namespace elog {

// the following special log field reference tokens can be used in configuration:
// ${rid} ${time} ${host} ${user} ${prog} ${pid} ${tid} ${src} ${msg}

// TODO: consider having a parent interface with no ctor params

/** @class Utility class for formatting log messages. */
class ELOG_API ELogBaseFormatter {
public:
    virtual ~ELogBaseFormatter();

    /**
     * @brief Initializes the log formatter.
     * @param logLineFormatSpec The log line format specification. The following special tokens are
     * interpreted as log record field references: ${rid} ${time} ${tid} ${src} ${msg}. The
     * following additional tokens are understood: ${host} for host name,
     * ${user} for logged in user, ${prog} for program name (executable image file name without
     * extension), ${pid} for current process id, and ${mod} for module name. More custom tokens can
     * be added by deriving from @ref ELogBaseFormatter and overriding the virtual method @ref
     * createFieldSelector().
     * @return true If the log line format specification was parsed successfully, otherwise false.
     */
    inline bool initialize(
        const char* logLineFormatSpec = "${time} ${level:6} [${tid}] ${src} ${msg}") {
        return parseFormatSpec(logLineFormatSpec);
    }

protected:
    ELogBaseFormatter() {}
    ELogBaseFormatter(const ELogBaseFormatter&) = delete;
    ELogBaseFormatter(ELogBaseFormatter&&) = delete;

    bool parseFormatSpec(const std::string& formatSpec);

    /**
     * @brief Select log record fields into a receptor.
     * @param logRecord The log record to format.
     * @param receptor The receiving end of the selector log record fields.
     * @param[out] logMsg The resulting formatted log message.
     */
    void applyFieldSelectors(const ELogRecord& logRecord, ELogFieldReceptor* receptor);

    // by default text within a format spec is transformed into static text field selector
    // but in the case of db formatter insert query this differs, so we allow this behavior to be
    // determined by derived classes
    virtual bool handleText(const std::string& text);

    virtual bool handleField(const char* fieldName, int justify);

    std::vector<ELogFieldSelector*> m_fieldSelectors;
};

}  // namespace elog

#endif  // __ELOG_BASE_FORMATTER_H__