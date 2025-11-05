#ifndef __ELOG_FIELD_SELECTOR_H___
#define __ELOG_FIELD_SELECTOR_H___

#include <vector>

#include "elog_field_receptor.h"
#include "elog_field_spec.h"
#include "elog_record.h"

#define ELOG_INVALID_FIELD_SELECTOR_TYPE_ID ((uint32_t)-1)

namespace elog {

// forward declaration
class ELOG_API ELogFilter;

/** @enum Constants for field types (generic). */
enum class ELogFieldType : uint32_t {
    /** @var Field type is string (text). */
    FT_TEXT,

    /** @var Field type is integer (64 bit). */
    FT_INT,

    /** @var Field type is date-time (can be stored as string though). */
    FT_DATETIME,

    /** @var Field type is a log level (32 bit). */
    FT_LOG_LEVEL,

    /** @var Field type is a formatting escape sequence. */
    FT_FORMAT
};

/**
 * @brief Parent interface for all field selectors. Custom selectors may be added by deriving from
 * this interface.
 */
class ELOG_API ELogFieldSelector {
public:
    /**
     * @brief Selects a field from the log record (or from external source) and appends it to the
     * message stream.
     * @param record The log record from which a field is to be selected.
     * @param receptor The receptor receiveing the selected log record field.
     */
    virtual void selectField(const ELogRecord& record, ELogFieldReceptor* receptor) = 0;

    /** @brief Retrieves the type of the selected field. */
    inline ELogFieldType getFieldType() const { return m_fieldType; }

    /** @brief Retrieves the specification of the selected field. */
    inline const ELogFieldSpec& getFieldSpec() const { return m_fieldSpec; }

    /**
     * @brief Allow for special cleanup, since field selector destruction is controlled (destructor
     * is forced as private through macro @ref ELOG_DECLARE_FIELD_SELECTOR()).
     */
    virtual void terminate() {}

protected:
    ELogFieldSelector(ELogFieldType fieldType, const ELogFieldSpec& fieldSpec = ELogFieldSpec())
        : m_fieldType(fieldType), m_fieldSpec(fieldSpec) {}
    ELogFieldSelector(const ELogFieldSelector&) = delete;
    ELogFieldSelector(ELogFieldSelector&&) = delete;
    ELogFieldSelector& operator=(const ELogFieldSelector&) = delete;
    virtual ~ELogFieldSelector() {}

    ELogFieldType m_fieldType;
    ELogFieldSpec m_fieldSpec;
};

// forward declaration
class ELOG_API ELogFieldSelectorConstructor;

/**
 * @brief Field selector constructor registration helper.
 * @param name The field selector identifier.
 * @param constructor The field selector constructor.
 */
extern ELOG_API void registerFieldSelectorConstructor(const char* name,
                                                      ELogFieldSelectorConstructor* constructor);

/**
 * @brief Utility helper for constructing a field selector from a field specification.
 * @param fieldSpec The field specification.
 * @return ELogFieldSelector* The resulting field selector, or null if failed.
 */
extern ELOG_API ELogFieldSelector* constructFieldSelector(const ELogFieldSpec& fieldSpec);

/** @brief Destroys a field selector object. */
extern ELOG_API void destroyFieldSelector(ELogFieldSelector* fieldSelector);

/** @brief Utility helper class for field selector construction. */
class ELOG_API ELogFieldSelectorConstructor {
public:
    virtual ~ELogFieldSelectorConstructor() {}

    /**
     * @brief Constructs a field selector.
     * @param fieldSpec The field specification.
     * @return ELogFieldSelector* The resulting field selector, or null if failed.
     */
    virtual ELogFieldSelector* constructFieldSelector(const ELogFieldSpec& fieldSpec) = 0;

    /** @brief Destroys a field selector object. */
    virtual void destroyFieldSelector(ELogFieldSelector* fieldSelector) = 0;

    /** @brief Installs field selector type id (for internal use only). */
    inline void setTypeId(uint32_t typeId) { m_typeId = typeId; }

    /** @brief Retrieves the field selector type id (for internal use only). */
    inline uint32_t getTypeId() const { return m_typeId; }

protected:
    /** @brief Constructor. */
    ELogFieldSelectorConstructor(const char* name) : m_typeId(0) {
        registerFieldSelectorConstructor(name, this);
    }
    ELogFieldSelectorConstructor(const ELogFieldSelectorConstructor&) = delete;
    ELogFieldSelectorConstructor(ELogFieldSelectorConstructor&&) = delete;
    ELogFieldSelectorConstructor& operator=(const ELogFieldSelectorConstructor&) = delete;

private:
    uint32_t m_typeId;
};

/**
 * @def Utility macro for declaring field selector factory method registration.
 * @param FieldSelectorType Type name of field selector.
 * @param Name Configuration name of field selector (for dynamic loading from configuration).
 * @param ImportExportSpec Window import/export specification. If exporting from a library then
 * specify a macro that will expand correctly within the library and from outside as well. If not
 * relevant then pass ELOG_NO_EXPORT.
 */
#define ELOG_DECLARE_FIELD_SELECTOR(FieldSelectorType, Name, ImportExportSpec)                     \
private:                                                                                           \
    ~FieldSelectorType() final {}                                                                  \
    friend class ImportExportSpec FieldSelectorType##Constructor;                                  \
    class ImportExportSpec FieldSelectorType##Constructor final                                    \
        : public elog::ELogFieldSelectorConstructor {                                              \
    public:                                                                                        \
        FieldSelectorType##Constructor() : elog::ELogFieldSelectorConstructor(#Name) {}            \
        elog::ELogFieldSelector* constructFieldSelector(                                           \
            const elog::ELogFieldSpec& fieldSpec) final;                                           \
        void destroyFieldSelector(elog::ELogFieldSelector* fieldSelector) final;                   \
        ~FieldSelectorType##Constructor() final {}                                                 \
        FieldSelectorType##Constructor(const FieldSelectorType##Constructor&) = delete;            \
        FieldSelectorType##Constructor(FieldSelectorType##Constructor&&) = delete;                 \
        FieldSelectorType##Constructor& operator=(const FieldSelectorType##Constructor&) = delete; \
    };                                                                                             \
    static FieldSelectorType##Constructor sConstructor;                                            \
                                                                                                   \
public:                                                                                            \
    static uint32_t getTypeId() { return sConstructor.getTypeId(); }

/** @def Utility macro for implementing field selector factory method registration. */
#define ELOG_IMPLEMENT_FIELD_SELECTOR(FieldSelectorType)                               \
    FieldSelectorType::FieldSelectorType##Constructor FieldSelectorType::sConstructor; \
    elog::ELogFieldSelector*                                                           \
        FieldSelectorType::FieldSelectorType##Constructor::constructFieldSelector(     \
            const elog::ELogFieldSpec& fieldSpec) {                                    \
        return new (std::nothrow) FieldSelectorType(fieldSpec);                        \
    }                                                                                  \
    void FieldSelectorType::FieldSelectorType##Constructor::destroyFieldSelector(      \
        elog::ELogFieldSelector* fieldSelector) {                                      \
        if (fieldSelector != nullptr) {                                                \
            fieldSelector->terminate();                                                \
            delete (FieldSelectorType*)fieldSelector;                                  \
        }                                                                              \
    }

/**
 * @brief Static text field selector, used for placing the strings between the fields in the log
 * format line specification string.
 */
class ELOG_API ELogStaticTextSelector final : public ELogFieldSelector {
public:
    ELogStaticTextSelector(const char* text = "")
        : ELogFieldSelector(ELogFieldType::FT_TEXT, ELogFieldSpec("text")), m_text(text) {}
    ELogStaticTextSelector(const ELogFieldSpec& fieldSpec)
        : ELogFieldSelector(ELogFieldType::FT_TEXT, fieldSpec) {}
    ELogStaticTextSelector(const ELogStaticTextSelector&) = delete;
    ELogStaticTextSelector(ELogStaticTextSelector&&) = delete;
    ELogStaticTextSelector& operator=(const ELogStaticTextSelector&) = delete;

    void selectField(const ELogRecord& record, ELogFieldReceptor* receptor) final;

private:
    std::string m_text;

    // we allow having ${text} as a keyword with no text context, solely for the purpose of allowing
    // specify text font/color specification
    ELOG_DECLARE_FIELD_SELECTOR(ELogStaticTextSelector, text, ELOG_API)
};

class ELOG_API ELogRecordIdSelector final : public ELogFieldSelector {
public:
    ELogRecordIdSelector(const ELogFieldSpec& fieldSpec)
        : ELogFieldSelector(ELogFieldType::FT_INT, fieldSpec) {}
    ELogRecordIdSelector(const ELogRecordIdSelector&) = delete;
    ELogRecordIdSelector(ELogRecordIdSelector&&) = delete;
    ELogRecordIdSelector& operator=(const ELogRecordIdSelector&) = delete;

    void selectField(const ELogRecord& record, ELogFieldReceptor* receptor) final;

private:
    ELOG_DECLARE_FIELD_SELECTOR(ELogRecordIdSelector, rid, ELOG_API)
};

class ELOG_API ELogTimeSelector final : public ELogFieldSelector {
public:
    ELogTimeSelector(const ELogFieldSpec& fieldSpec)
        : ELogFieldSelector(ELogFieldType::FT_DATETIME, fieldSpec) {}
    ELogTimeSelector(const ELogTimeSelector&) = delete;
    ELogTimeSelector(ELogTimeSelector&&) = delete;
    ELogTimeSelector& operator=(const ELogTimeSelector&) = delete;

    void selectField(const ELogRecord& record, ELogFieldReceptor* receptor) final;

private:
    ELOG_DECLARE_FIELD_SELECTOR(ELogTimeSelector, time, ELOG_API)
};

class ELOG_API ELogTimeEpochSelector final : public ELogFieldSelector {
public:
    ELogTimeEpochSelector(const ELogFieldSpec& fieldSpec)
        : ELogFieldSelector(ELogFieldType::FT_INT, fieldSpec) {}
    ELogTimeEpochSelector(const ELogTimeEpochSelector&) = delete;
    ELogTimeEpochSelector(ELogTimeEpochSelector&&) = delete;
    ELogTimeEpochSelector& operator=(const ELogTimeEpochSelector&) = delete;

    void selectField(const ELogRecord& record, ELogFieldReceptor* receptor) final;

private:
    ELOG_DECLARE_FIELD_SELECTOR(ELogTimeEpochSelector, time_epoch, ELOG_API)
};

class ELOG_API ELogHostNameSelector final : public ELogFieldSelector {
public:
    ELogHostNameSelector(const ELogFieldSpec& fieldSpec)
        : ELogFieldSelector(ELogFieldType::FT_TEXT, fieldSpec) {}
    ELogHostNameSelector(const ELogHostNameSelector&) = delete;
    ELogHostNameSelector(ELogHostNameSelector&&) = delete;
    ELogHostNameSelector& operator=(const ELogHostNameSelector&) = delete;

    void selectField(const ELogRecord& record, ELogFieldReceptor* receptor) final;

private:
    ELOG_DECLARE_FIELD_SELECTOR(ELogHostNameSelector, host, ELOG_API)
};

class ELOG_API ELogUserNameSelector final : public ELogFieldSelector {
public:
    ELogUserNameSelector(const ELogFieldSpec& fieldSpec)
        : ELogFieldSelector(ELogFieldType::FT_TEXT, fieldSpec) {}
    ELogUserNameSelector(const ELogUserNameSelector&) = delete;
    ELogUserNameSelector(ELogUserNameSelector&&) = delete;
    ELogUserNameSelector& operator=(const ELogUserNameSelector&) = delete;

    void selectField(const ELogRecord& record, ELogFieldReceptor* receptor) final;

private:
    ELOG_DECLARE_FIELD_SELECTOR(ELogUserNameSelector, user, ELOG_API)
};

class ELOG_API ELogOsNameSelector final : public ELogFieldSelector {
public:
    ELogOsNameSelector(const ELogFieldSpec& fieldSpec)
        : ELogFieldSelector(ELogFieldType::FT_TEXT, fieldSpec) {}
    ELogOsNameSelector(const ELogOsNameSelector&) = delete;
    ELogOsNameSelector(ELogOsNameSelector&&) = delete;
    ELogOsNameSelector& operator=(const ELogOsNameSelector&) = delete;

    void selectField(const ELogRecord& record, ELogFieldReceptor* receptor) final;

private:
    ELOG_DECLARE_FIELD_SELECTOR(ELogOsNameSelector, os_name, ELOG_API)
};

class ELOG_API ELogOsVersionSelector final : public ELogFieldSelector {
public:
    ELogOsVersionSelector(const ELogFieldSpec& fieldSpec)
        : ELogFieldSelector(ELogFieldType::FT_TEXT, fieldSpec) {}
    ELogOsVersionSelector(const ELogOsVersionSelector&) = delete;
    ELogOsVersionSelector(ELogOsVersionSelector&&) = delete;
    ELogOsVersionSelector& operator=(const ELogOsVersionSelector&) = delete;

    void selectField(const ELogRecord& record, ELogFieldReceptor* receptor) final;

private:
    ELOG_DECLARE_FIELD_SELECTOR(ELogOsVersionSelector, os_ver, ELOG_API)
};

class ELOG_API ELogAppNameSelector final : public ELogFieldSelector {
public:
    ELogAppNameSelector(const ELogFieldSpec& fieldSpec)
        : ELogFieldSelector(ELogFieldType::FT_TEXT, fieldSpec) {}
    ELogAppNameSelector(const ELogAppNameSelector&) = delete;
    ELogAppNameSelector(ELogAppNameSelector&&) = delete;
    ELogAppNameSelector& operator=(const ELogAppNameSelector&) = delete;

    void selectField(const ELogRecord& record, ELogFieldReceptor* receptor) final;

private:
    ELOG_DECLARE_FIELD_SELECTOR(ELogAppNameSelector, app, ELOG_API)
};

class ELOG_API ELogProgramNameSelector final : public ELogFieldSelector {
public:
    ELogProgramNameSelector(const ELogFieldSpec& fieldSpec)
        : ELogFieldSelector(ELogFieldType::FT_TEXT, fieldSpec) {}
    ELogProgramNameSelector(const ELogProgramNameSelector&) = delete;
    ELogProgramNameSelector(ELogProgramNameSelector&&) = delete;
    ELogProgramNameSelector& operator=(const ELogProgramNameSelector&) = delete;

    void selectField(const ELogRecord& record, ELogFieldReceptor* receptor) final;

private:
    ELOG_DECLARE_FIELD_SELECTOR(ELogProgramNameSelector, prog, ELOG_API)
};

class ELOG_API ELogProcessIdSelector final : public ELogFieldSelector {
public:
    ELogProcessIdSelector(const ELogFieldSpec& fieldSpec)
        : ELogFieldSelector(ELogFieldType::FT_INT, fieldSpec) {}
    ELogProcessIdSelector(const ELogProcessIdSelector&) = delete;
    ELogProcessIdSelector(ELogProcessIdSelector&&) = delete;
    ELogProcessIdSelector& operator=(const ELogProcessIdSelector&) = delete;

    void selectField(const ELogRecord& record, ELogFieldReceptor* receptor) final;

private:
    ELOG_DECLARE_FIELD_SELECTOR(ELogProcessIdSelector, pid, ELOG_API)
};

class ELOG_API ELogThreadIdSelector final : public ELogFieldSelector {
public:
    ELogThreadIdSelector(const ELogFieldSpec& fieldSpec)
        : ELogFieldSelector(ELogFieldType::FT_INT, fieldSpec) {}
    ELogThreadIdSelector(const ELogThreadIdSelector&) = delete;
    ELogThreadIdSelector(ELogThreadIdSelector&&) = delete;
    ELogThreadIdSelector& operator=(const ELogThreadIdSelector&) = delete;

    void selectField(const ELogRecord& record, ELogFieldReceptor* receptor) final;

private:
    ELOG_DECLARE_FIELD_SELECTOR(ELogThreadIdSelector, tid, ELOG_API)
};

class ELOG_API ELogThreadNameSelector final : public ELogFieldSelector {
public:
    ELogThreadNameSelector(const ELogFieldSpec& fieldSpec)
        : ELogFieldSelector(ELogFieldType::FT_TEXT, fieldSpec) {}
    ELogThreadNameSelector(const ELogThreadNameSelector&) = delete;
    ELogThreadNameSelector(ELogThreadNameSelector&&) = delete;
    ELogThreadNameSelector& operator=(const ELogThreadNameSelector&) = delete;

    void selectField(const ELogRecord& record, ELogFieldReceptor* receptor) final;

private:
    ELOG_DECLARE_FIELD_SELECTOR(ELogThreadNameSelector, tname, ELOG_API)
};

class ELOG_API ELogSourceSelector final : public ELogFieldSelector {
public:
    ELogSourceSelector(const ELogFieldSpec& fieldSpec)
        : ELogFieldSelector(ELogFieldType::FT_TEXT, fieldSpec) {}
    ELogSourceSelector(const ELogSourceSelector&) = delete;
    ELogSourceSelector(ELogSourceSelector&&) = delete;
    ELogSourceSelector& operator=(const ELogSourceSelector&) = delete;

    void selectField(const ELogRecord& record, ELogFieldReceptor* receptor) final;

private:
    ELOG_DECLARE_FIELD_SELECTOR(ELogSourceSelector, src, ELOG_API)
};

class ELOG_API ELogModuleSelector final : public ELogFieldSelector {
public:
    ELogModuleSelector(const ELogFieldSpec& fieldSpec)
        : ELogFieldSelector(ELogFieldType::FT_TEXT, fieldSpec) {}
    ELogModuleSelector(const ELogModuleSelector&) = delete;
    ELogModuleSelector(ELogModuleSelector&&) = delete;
    ELogModuleSelector& operator=(const ELogModuleSelector&) = delete;

    void selectField(const ELogRecord& record, ELogFieldReceptor* receptor) final;

private:
    ELOG_DECLARE_FIELD_SELECTOR(ELogModuleSelector, mod, ELOG_API)
};

class ELOG_API ELogFileSelector final : public ELogFieldSelector {
public:
    ELogFileSelector(const ELogFieldSpec& fieldSpec)
        : ELogFieldSelector(ELogFieldType::FT_TEXT, fieldSpec) {}
    ELogFileSelector(const ELogFileSelector&) = delete;
    ELogFileSelector(ELogFileSelector&&) = delete;
    ELogFileSelector& operator=(const ELogFileSelector&) = delete;

    void selectField(const ELogRecord& record, ELogFieldReceptor* receptor) final;

private:
    ELOG_DECLARE_FIELD_SELECTOR(ELogFileSelector, file, ELOG_API)
};

class ELOG_API ELogLineSelector final : public ELogFieldSelector {
public:
    ELogLineSelector(const ELogFieldSpec& fieldSpec)
        : ELogFieldSelector(ELogFieldType::FT_INT, fieldSpec) {}
    ELogLineSelector(const ELogLineSelector&) = delete;
    ELogLineSelector(ELogLineSelector&&) = delete;
    ELogLineSelector& operator=(const ELogLineSelector&) = delete;

    void selectField(const ELogRecord& record, ELogFieldReceptor* receptor) final;

private:
    ELOG_DECLARE_FIELD_SELECTOR(ELogLineSelector, line, ELOG_API)
};

class ELOG_API ELogFunctionSelector final : public ELogFieldSelector {
public:
    ELogFunctionSelector(const ELogFieldSpec& fieldSpec)
        : ELogFieldSelector(ELogFieldType::FT_TEXT, fieldSpec) {}
    ELogFunctionSelector(const ELogFunctionSelector&) = delete;
    ELogFunctionSelector(ELogFunctionSelector&&) = delete;
    ELogFunctionSelector& operator=(const ELogFunctionSelector&) = delete;

    void selectField(const ELogRecord& record, ELogFieldReceptor* receptor) final;

private:
    ELOG_DECLARE_FIELD_SELECTOR(ELogFunctionSelector, func, ELOG_API)
};

class ELOG_API ELogLevelSelector final : public ELogFieldSelector {
public:
    ELogLevelSelector(const ELogFieldSpec& fieldSpec)
        : ELogFieldSelector(ELogFieldType::FT_LOG_LEVEL, fieldSpec) {}
    ELogLevelSelector(const ELogLevelSelector&) = delete;
    ELogLevelSelector(ELogLevelSelector&&) = delete;
    ELogLevelSelector& operator=(const ELogLevelSelector&) = delete;

    void selectField(const ELogRecord& record, ELogFieldReceptor* receptor) final;

private:
    ELOG_DECLARE_FIELD_SELECTOR(ELogLevelSelector, level, ELOG_API)
};

class ELOG_API ELogMsgSelector final : public ELogFieldSelector {
public:
    ELogMsgSelector(const ELogFieldSpec& fieldSpec)
        : ELogFieldSelector(ELogFieldType::FT_TEXT, fieldSpec) {}
    ELogMsgSelector(const ELogMsgSelector&) = delete;
    ELogMsgSelector(ELogMsgSelector&&) = delete;
    ELogMsgSelector& operator=(const ELogMsgSelector&) = delete;

    void selectField(const ELogRecord& record, ELogFieldReceptor* receptor) final;

private:
    ELOG_DECLARE_FIELD_SELECTOR(ELogMsgSelector, msg, ELOG_API)
};

class ELOG_API ELogEnvSelector final : public ELogFieldSelector {
public:
    ELogEnvSelector(const ELogFieldSpec& fieldSpec)
        : ELogFieldSelector(ELogFieldType::FT_LOG_LEVEL, fieldSpec) {}
    ELogEnvSelector(const ELogEnvSelector&) = delete;
    ELogEnvSelector(ELogEnvSelector&&) = delete;
    ELogEnvSelector& operator=(const ELogEnvSelector&) = delete;

    void selectField(const ELogRecord& record, ELogFieldReceptor* receptor) final;

private:
    ELOG_DECLARE_FIELD_SELECTOR(ELogEnvSelector, env, ELOG_API)
};

/**
 * Text Formatting virtual field selector. The following field selectors do not select fields
 * (either from the log record, or from any other custom source), but rather output text formatting
 * escape sequences. All format selectors output text field type (the escape code sequence).
 */

/** @brief Format text field selector. */
class ELOG_API ELogFormatSelector final : public ELogFieldSelector {
public:
    ELogFormatSelector(const ELogFieldSpec& fieldSpec)
        : ELogFieldSelector(ELogFieldType::FT_TEXT, fieldSpec) {}
    ELogFormatSelector(const ELogFormatSelector&) = delete;
    ELogFormatSelector(ELogFormatSelector&&) = delete;
    ELogFormatSelector& operator=(const ELogFormatSelector&) = delete;

    void selectField(const ELogRecord& record, ELogFieldReceptor* receptor) final;

private:
    // we allow having ${fmt} as a keyword, solely for the purpose of allowing specify text
    // font/color specification
    ELOG_DECLARE_FIELD_SELECTOR(ELogFormatSelector, fmt, ELOG_API)
};

/**
 * @brief Conditional field selector. Can be used for conditional formatting (i.e. no text emitted
 * except for formatting escape codes).
 */
class ELOG_API ELogIfSelector final : public ELogFieldSelector {
public:
    ELogIfSelector(const ELogFieldSpec& fieldSpec)
        : ELogFieldSelector(ELogFieldType::FT_FORMAT, fieldSpec),
          m_cond(nullptr),
          m_trueSelector(nullptr),
          m_falseSelector(nullptr) {}

    ELogIfSelector(ELogFilter* cond, ELogFieldSelector* trueSelector,
                   ELogFieldSelector* falseSelector = nullptr)
        : ELogFieldSelector(trueSelector->getFieldType(), trueSelector->getFieldSpec()),
          m_cond(cond),
          m_trueSelector(trueSelector),
          m_falseSelector(falseSelector) {}

    ELogIfSelector(const ELogIfSelector&) = delete;
    ELogIfSelector(ELogIfSelector&&) = delete;
    ELogIfSelector& operator=(const ELogIfSelector&) = delete;

    void selectField(const ELogRecord& record, ELogFieldReceptor* receptor) final;

    /**
     * @brief Allow for special cleanup, since field selector destruction is controlled (destructor
     * is forced as private through macro @ref ELOG_DECLARE_FIELD_SELECTOR()).
     */
    void terminate() final;

private:
    // parent class's m_fieldSpec member holds all following 3 members (3rd optional)
    ELogFilter* m_cond;
    ELogFieldSelector* m_trueSelector;
    ELogFieldSelector* m_falseSelector;

    ELOG_DECLARE_FIELD_SELECTOR(ELogIfSelector, if, ELOG_API)
};

/** @brief Switch-case field selector. Can be used also for conditional formatting. */
class ELOG_API ELogSwitchSelector final : public ELogFieldSelector {
public:
    ELogSwitchSelector(const ELogFieldSpec& fieldSpec)
        : ELogFieldSelector(ELogFieldType::FT_FORMAT, fieldSpec),
          m_valueExpr(nullptr),
          m_defaultFieldSelector(nullptr) {}

    ELogSwitchSelector(ELogFieldSelector* valueExpr)
        : ELogFieldSelector(valueExpr->getFieldType(), valueExpr->getFieldSpec()),
          m_valueExpr(valueExpr),
          m_defaultFieldSelector(nullptr) {}

    ELogSwitchSelector(const ELogSwitchSelector&) = delete;
    ELogSwitchSelector(ELogSwitchSelector&&) = delete;
    ELogSwitchSelector& operator=(const ELogSwitchSelector&) = delete;

    inline void addCase(ELogFieldSelector* caseValueExpr, ELogFieldSelector* caseFieldSelector) {
        m_cases.push_back({caseValueExpr, caseFieldSelector});
    }

    inline void addDefaultCase(ELogFieldSelector* defaultFieldSelector) {
        m_defaultFieldSelector = defaultFieldSelector;
    }

    void selectField(const ELogRecord& record, ELogFieldReceptor* receptor) final;

    /**
     * @brief Allow for special cleanup, since field selector destruction is controlled (destructor
     * is forced as private through macro @ref ELOG_DECLARE_FIELD_SELECTOR()).
     */
    void terminate() final;

private:
    // parent class's m_fieldSpec member holds all following 3 members (3rd optional)
    ELogFieldSelector* m_valueExpr;
    std::vector<std::pair<ELogFieldSelector*, ELogFieldSelector*>> m_cases;
    ELogFieldSelector* m_defaultFieldSelector;

    ELOG_DECLARE_FIELD_SELECTOR(ELogSwitchSelector, switch, ELOG_API)
};

/** @brief Switch-case field selector. */
class ELOG_API ELogExprSwitchSelector final : public ELogFieldSelector {
public:
    ELogExprSwitchSelector(const ELogFieldSpec& fieldSpec)
        : ELogFieldSelector(ELogFieldType::FT_FORMAT, fieldSpec), m_defaultFieldSelector(nullptr) {}
    ELogExprSwitchSelector()
        : ELogFieldSelector(ELogFieldType::FT_FORMAT, ELogFieldSpec("expr-switch")),
          m_defaultFieldSelector(nullptr) {}
    ELogExprSwitchSelector(const ELogExprSwitchSelector&) = delete;
    ELogExprSwitchSelector(ELogExprSwitchSelector&&) = delete;
    ELogExprSwitchSelector& operator=(const ELogExprSwitchSelector&) = delete;

    inline void addCase(ELogFilter* casePred, ELogFieldSelector* caseFieldSelector) {
        m_cases.push_back({casePred, caseFieldSelector});
    }

    inline void addDefaultCase(ELogFieldSelector* defaultFieldSelector) {
        m_defaultFieldSelector = defaultFieldSelector;
    }

    void selectField(const ELogRecord& record, ELogFieldReceptor* receptor) final;

    /**
     * @brief Allow for special cleanup, since field selector destruction is controlled (destructor
     * is forced as private through macro @ref ELOG_DECLARE_FIELD_SELECTOR()).
     */
    void terminate() final;

private:
    std::vector<std::pair<ELogFilter*, ELogFieldSelector*>> m_cases;
    ELogFieldSelector* m_defaultFieldSelector;

    // turn off clang formatting due to "expr-switch" below
    // clang-format off
    ELOG_DECLARE_FIELD_SELECTOR(ELogExprSwitchSelector, expr-switch, ELOG_API)
    // clang-format on
};

/** @brief Constant string field selector. */
class ELOG_API ELogConstStringSelector final : public ELogFieldSelector {
public:
    ELogConstStringSelector(const char* value)
        : ELogFieldSelector(ELogFieldType::FT_TEXT, ELogFieldSpec("const-string")),
          m_constString(value) {}
    ELogConstStringSelector(const ELogFieldSpec& fieldSpec)
        : ELogFieldSelector(ELogFieldType::FT_TEXT, fieldSpec) {}
    ELogConstStringSelector(const ELogConstStringSelector&) = delete;
    ELogConstStringSelector(ELogConstStringSelector&&) = delete;
    ELogConstStringSelector& operator=(const ELogConstStringSelector&) = delete;

    void selectField(const ELogRecord& record, ELogFieldReceptor* receptor) final;

private:
    std::string m_constString;

    // turn off clang formatting due to "const-string" below
    // clang-format off
    ELOG_DECLARE_FIELD_SELECTOR(ELogConstStringSelector, const-string, ELOG_API)
    // clang-format on
};

/** @brief Constant integer field selector. */
class ELOG_API ELogConstIntSelector final : public ELogFieldSelector {
public:
    ELogConstIntSelector(uint64_t value)
        : ELogFieldSelector(ELogFieldType::FT_INT, ELogFieldSpec("const-int")), m_constInt(value) {}
    ELogConstIntSelector(const ELogFieldSpec& fieldSpec)
        : ELogFieldSelector(ELogFieldType::FT_INT, fieldSpec) {}
    ELogConstIntSelector(const ELogConstIntSelector&) = delete;
    ELogConstIntSelector(ELogConstIntSelector&&) = delete;
    ELogConstIntSelector& operator=(const ELogConstIntSelector&) = delete;

    void selectField(const ELogRecord& record, ELogFieldReceptor* receptor) final;

private:
    uint64_t m_constInt;

    // turn off clang formatting due to "const-int" below
    // clang-format off
    ELOG_DECLARE_FIELD_SELECTOR(ELogConstIntSelector, const-int, ELOG_API)
    // clang-format on
};

/** @brief Constant time field selector. */
class ELOG_API ELogConstTimeSelector final : public ELogFieldSelector {
public:
    ELogConstTimeSelector(const ELogTime& value, const char* timeStr)
        : ELogFieldSelector(ELogFieldType::FT_DATETIME, ELogFieldSpec("const-time")),
          m_constTime(value),
          m_timeStr(timeStr) {}
    ELogConstTimeSelector(const ELogFieldSpec& fieldSpec)
        : ELogFieldSelector(ELogFieldType::FT_DATETIME, fieldSpec) {}
    ELogConstTimeSelector(const ELogConstTimeSelector&) = delete;
    ELogConstTimeSelector(ELogConstTimeSelector&&) = delete;
    ELogConstTimeSelector& operator=(const ELogConstTimeSelector&) = delete;

    void selectField(const ELogRecord& record, ELogFieldReceptor* receptor) final;

private:
    ELogTime m_constTime;
    std::string m_timeStr;

    // turn off clang formatting due to "const-time" below
    // clang-format off
    ELOG_DECLARE_FIELD_SELECTOR(ELogConstTimeSelector, const-time, ELOG_API)
    // clang-format on
};

/** @brief Constant log-level field selector. */
class ELOG_API ELogConstLogLevelSelector final : public ELogFieldSelector {
public:
    ELogConstLogLevelSelector(const ELogLevel value)
        : ELogFieldSelector(ELogFieldType::FT_LOG_LEVEL, ELogFieldSpec("const-level")),
          m_constLevel(value) {}
    ELogConstLogLevelSelector(const ELogFieldSpec& fieldSpec)
        : ELogFieldSelector(ELogFieldType::FT_LOG_LEVEL, fieldSpec) {}
    ELogConstLogLevelSelector(const ELogConstLogLevelSelector&) = delete;
    ELogConstLogLevelSelector(ELogConstLogLevelSelector&&) = delete;
    ELogConstLogLevelSelector& operator=(const ELogConstLogLevelSelector&) = delete;

    void selectField(const ELogRecord& record, ELogFieldReceptor* receptor) final;

private:
    ELogLevel m_constLevel;

    // turn off clang formatting due to "const-level" below
    // clang-format off
    ELOG_DECLARE_FIELD_SELECTOR(ELogConstLogLevelSelector, const-level, ELOG_API)
    // clang-format on
};

}  // namespace elog

#endif  // __ELOG_FIELD_SELECTOR_H___