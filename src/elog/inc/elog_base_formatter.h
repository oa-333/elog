#ifndef __ELOG_BASE_FORMATTER_H__
#define __ELOG_BASE_FORMATTER_H__

#include <string>
#include <vector>

#include "elog_field_selector.h"
#include "elog_managed_object.h"
#include "elog_record.h"

namespace elog {

// the following special log field reference tokens can be used in configuration:
// ${rid} ${time} ${host} ${user} ${prog} ${pid} ${tid} ${src} ${msg}

/** @class Utility class for formatting log messages. */
class ELOG_API ELogBaseFormatter : public ELogManagedObject {
public:
    ~ELogBaseFormatter() override;

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
        const char* logLineFormatSpec = "${time} ${level:6} [${tid:5}] ${src} ${msg}") {
        return parseFormatSpec(logLineFormatSpec);
    }

protected:
    ELogBaseFormatter() {}
    ELogBaseFormatter(const ELogBaseFormatter&) = delete;
    ELogBaseFormatter(ELogBaseFormatter&&) = delete;
    ELogBaseFormatter& operator=(const ELogBaseFormatter&) = delete;

    bool parseFormatSpec(const std::string& formatSpec);

    bool parseFieldSpec(const std::string& fieldSpecStr);

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

    /**
     * @brief Reacts to log format text parsed event. When overriding this method, sub-classed must
     * call the parent method @ref ELogBaseFormatter::handleText().
     * @note By default text within a format specification is transformed into static text field
     * selector. Some formatters (e.g. @ref ELogDbFormatter) require further handling, so this
     * method is made virtual.
     * @param text The parsed text.
     * @return The operation result.
     */
    virtual bool handleText(const std::string& text);

    /**
     * @brief Reacts to log record field reference parsed event. When overriding this method,
     * sub-classes must call the parent method @ref ELogBaseFormatter::handleField().
     * @note By default field reference within a format specification is transformed into a field
     * selector. Some formatters (e.g. @ref ELogDbFormatter) require further handling, so this
     * method is made virtual.
     * @param fieldSpec The parsed field.
     * @return The operation result.
     */
    virtual bool handleField(const ELogFieldSpec& fieldSpec);

    /** @brief Parses a value either as a reference token, or text. */
    bool parseValue(const std::string& value);

    /** @brief The field selectors. */
    std::vector<ELogFieldSelector*> m_fieldSelectors;

private:
    bool getFieldCloseBrace(const std::string& formatSpec, std::string::size_type from,
                            std::string::size_type& closePos);
    bool getFieldCloseParen(const std::string& formatSpec, std::string::size_type from,
                            std::string::size_type& closePos);
    bool parseSimpleField(const std::string& fieldSpecStr);
    bool parseCondField(const std::string& fieldSpecStr);
    bool parseSwitchField(const std::string& fieldSpecStr);
    bool parseExprSwitchField(const std::string& fieldSpecStr);
    bool parseCaseOrDefaultClause(ELogSwitchSelector* switchSelector, const std::string& caseSpec,
                                  bool& isDefaultClause);
    bool parseCaseClause(ELogSwitchSelector* switchSelector, const std::string& caseSpec);
    bool parseDefaultClause(ELogSwitchSelector* switchSelector, const std::string& defaultSpec);
    bool parseExprCaseOrDefaultClause(ELogExprSwitchSelector* switchSelector,
                                      const std::string& caseSpec, bool& isDefaultClause);
    bool parseExprCaseClause(ELogExprSwitchSelector* switchSelector, const std::string& caseSpec);
    bool parseExprDefaultClause(ELogExprSwitchSelector* switchSelector,
                                const std::string& defaultSpec);
    ELogFieldSelector* loadSelector(const std::string& selectorSpecStr);
    ELogFieldSelector* loadConstSelector(const std::string& fieldSpecStr);
};

}  // namespace elog

#endif  // __ELOG_BASE_FORMATTER_H__