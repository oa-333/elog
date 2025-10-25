#ifndef __ELOG_BASE_FORMATTER_H__
#define __ELOG_BASE_FORMATTER_H__

#include <string>
#include <vector>

#include "elog_buffer.h"
#include "elog_field_selector.h"
#include "elog_managed_object.h"
#include "elog_record.h"

namespace elog {

// the following special log field reference tokens can be used in configuration:
// ${rid} ${time} ${host} ${user} ${prog} ${pid} ${tid} ${src} ${msg}

#define ELOG_DEFAULT_FORMATTER_TYPE_NAME "default"

/** @class Utility class for formatting log messages. */
class ELOG_API ELogFormatter : public ELogManagedObject {
public:
    ELogFormatter(const char* typeName = ELOG_DEFAULT_FORMATTER_TYPE_NAME) : m_typeName(typeName) {}
    ELogFormatter(const ELogFormatter&) = delete;
    ELogFormatter(ELogFormatter&&) = delete;
    ELogFormatter& operator=(const ELogFormatter&) = delete;

    /**
     * @brief Initializes the log formatter.
     * @param logLineFormatSpec The log line format specification. The following special tokens are
     * interpreted as log record field references: ${rid} ${time} ${tid} ${src} ${msg}. The
     * following additional tokens are understood: ${host} for host name,
     * ${user} for logged in user, ${prog} for program name (executable image file name without
     * extension), ${pid} for current process id, and ${mod} for module name.
     * @return true If the log line format specification was parsed successfully, otherwise false.
     */
    inline bool initialize(
        const char* logLineFormatSpec = "${time} ${level:6} [${tid:5}] ${src} ${msg}") {
        return parseFormatSpec(logLineFormatSpec);
    }

    /**
     * @brief Formats a log message into a string from a log records using the formatter.
     * @param logRecord The log record used for formatting.
     * @param[out] logMsg The resulting formatted log message.
     */
    virtual void formatLogMsg(const ELogRecord& logRecord, std::string& logMsg);

    /**
     * @brief Formats a log message into a log buffer from a log records using the formatter.
     * @param logRecord The log record used for formatting.
     * @param[out] logBuffer The resulting formatted log buffer.
     */
    virtual void formatLogBuffer(const ELogRecord& logRecord, ELogBuffer& logBuffer);

    /**
     * @brief Allow for special cleanup, since log formatter destruction is controlled (destructor
     * not exposed).
     */
    virtual void destroy() {}

    /**
     * @brief Select log record fields into a receptor.
     * @param logRecord The log record to format.
     * @param receptor The receiving end of the selector log record fields.
     */
    void applyFieldSelectors(const ELogRecord& logRecord, ELogFieldReceptor* receptor);

    /** @brief Retrieves the type name of the formatter. */
    inline const char* getTypeName() const { return m_typeName.c_str(); }

protected:
    ~ELogFormatter() override;

    bool parseFormatSpec(const std::string& formatSpec);

    bool parseFieldSpec(const std::string& fieldSpecStr);

    // by default text within a format spec is transformed into static text field selector
    // but in the case of db formatter insert query this differs, so we allow this behavior to be
    // determined by derived classes

    /**
     * @brief Reacts to log format text parsed event. When overriding this method, sub-classed must
     * call the parent method @ref ELogFormatter::handleText().
     * @note By default text within a format specification is transformed into static text field
     * selector. Some formatters (e.g. @ref ELogDbFormatter) require further handling, so this
     * method is made virtual.
     * @param text The parsed text.
     * @return The operation result.
     */
    virtual bool handleText(const std::string& text);

    /**
     * @brief Reacts to log record field reference parsed event. When overriding this method,
     * sub-classes must call the parent method @ref ELogFormatter::handleField().
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

    std::string m_typeName;

    friend ELOG_API void destroyLogFormatter(ELogFormatter* formatter);
};

// forward declaration
class ELOG_API ELogFormatterConstructor;

/**
 * @brief Log formatter constructor registration helper.
 * @param name The log formatter identifier.
 * @param constructor The log formatter constructor.
 */
extern ELOG_API void registerLogFormatterConstructor(const char* name,
                                                     ELogFormatterConstructor* constructor);

/**
 * @brief Utility helper for constructing a log formatter from type name identifier.
 * @param name The log formatter identifier.
 * @param issueErrors Optionally specifies whether to issue errors when encountered or not (will
 * still be issues as trace reports).
 * @return ELogFormatter* The resulting log formatter, or null if failed.
 */
extern ELOG_API ELogFormatter* constructLogFormatter(const char* name, bool issueErrors = true);

/** @brief Destroys a log formatter object. */
extern ELOG_API void destroyLogFormatter(ELogFormatter* formatter);

/** @brief Utility helper class for log formatter construction. */
class ELOG_API ELogFormatterConstructor {
public:
    virtual ~ELogFormatterConstructor() {}

    /**
     * @brief Constructs a log formatter.
     * @return ELogFormatter* The resulting log formatter, or null if failed.
     */
    virtual ELogFormatter* constructFormatter() = 0;

    /** @brief Destroys a log formatter object. */
    virtual void destroyFormatter(ELogFormatter* formatter) = 0;

protected:
    /** @brief Constructor. */
    ELogFormatterConstructor(const char* name) { registerLogFormatterConstructor(name, this); }
    ELogFormatterConstructor(const ELogFormatterConstructor&) = delete;
    ELogFormatterConstructor(ELogFormatterConstructor&&) = delete;
    ELogFormatterConstructor& operator=(const ELogFormatterConstructor&) = delete;
};

// TODO: for sake of being able to externally extend elog, the ELOG_API should be replaced with
// macro parameter, so it can be set to dll export, or to nothing

/** @def Utility macro for declaring log formatter factory method registration. */
#define ELOG_DECLARE_LOG_FORMATTER(FormatterType, Name, ImportExportSpec)                  \
    ~FormatterType() final {}                                                              \
    friend class ImportExportSpec FormatterType##Constructor;                              \
    class ImportExportSpec FormatterType##Constructor final                                \
        : public elog::ELogFormatterConstructor {                                          \
    public:                                                                                \
        FormatterType##Constructor() : elog::ELogFormatterConstructor(#Name) {}            \
        elog::ELogFormatter* constructFormatter() final;                                   \
        void destroyFormatter(elog::ELogFormatter* formatter) final;                       \
        ~FormatterType##Constructor() final {}                                             \
        FormatterType##Constructor(const FormatterType##Constructor&) = delete;            \
        FormatterType##Constructor(FormatterType##Constructor&&) = delete;                 \
        FormatterType##Constructor& operator=(const FormatterType##Constructor&) = delete; \
    };                                                                                     \
    static FormatterType##Constructor sConstructor;

/** @def Utility macro for implementing log formatter factory method registration. */
#define ELOG_IMPLEMENT_LOG_FORMATTER(FormatterType)                                        \
    FormatterType::FormatterType##Constructor FormatterType::sConstructor;                 \
    elog::ELogFormatter* FormatterType::FormatterType##Constructor::constructFormatter() { \
        return new (std::nothrow) FormatterType();                                         \
    }                                                                                      \
    void FormatterType::FormatterType##Constructor::destroyFormatter(                      \
        elog::ELogFormatter* formatter) {                                                  \
        if (formatter != nullptr) {                                                        \
            formatter->destroy();                                                          \
            delete (FormatterType*)formatter;                                              \
        }                                                                                  \
    }

}  // namespace elog

#endif  // __ELOG_BASE_FORMATTER_H__