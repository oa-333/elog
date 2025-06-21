#ifndef __ELOG_FIELD_SELECTOR_H___
#define __ELOG_FIELD_SELECTOR_H___

#include "elog_field_receptor.h"
#include "elog_field_spec.h"
#include "elog_record.h"

#define ELOG_INVALID_FIELD_SELECTOR_TYPE_ID ((uint32_t)-1)

namespace elog {

/** @enum Constants for field types (generic). */
enum class ELogFieldType : uint32_t {
    /** @var Field type is string (text). */
    FT_TEXT,

    /** @var Field type is integer (64 bit). */
    FT_INT,

    /** @var Field type is date-time (can be stored as string though). */
    FT_DATETIME
};

/**
 * @brief Parent interface for all field selectors. Custom selectors may be added by deriving from
 * this interface and deriving from @ref ELogFormatter, so that the field can be parsed by
 * overriding the virtual function @ref createFieldSelector().
 */
class ELOG_API ELogFieldSelector {
public:
    virtual ~ELogFieldSelector() {}

    /**
     * @brief Selects a field from the log record (or from external source) and appends it to the
     * message stream.
     * @param record The log record from which a field is to be selected.
     * @param receptor The receptor receiveing the selected log record field.
     */
    virtual void selectField(const ELogRecord& record, ELogFieldReceptor* receptor) = 0;

    /** @brief Retrieves the type of the selected field. */
    inline ELogFieldType getFieldType() const { return m_fieldType; }

protected:
    ELogFieldSelector(ELogFieldType fieldType, const ELogFieldSpec& fieldSpec = ELogFieldSpec())
        : m_fieldType(fieldType), m_fieldSpec(fieldSpec) {}

    ELogFieldType m_fieldType;
    ELogFieldSpec m_fieldSpec;
};

// forward declaration
class ELOG_API ELogFieldSelectorConstructor;

/**
 * @brief Field selector constructor registration helper.
 * @param name The field selector identifier.
 * @param allocator The field selector constructor.
 */
extern ELOG_API void registerFieldSelectorConstructor(const char* name,
                                                      ELogFieldSelectorConstructor* constructor);

/**
 * @brief Utility helper for constructing a field selector from a field specification.
 * @param fieldSpec The field specification.
 * @return ELogFieldSelector* The resulting field selector, or null if failed.
 */
extern ELOG_API ELogFieldSelector* constructFieldSelector(const ELogFieldSpec& fieldSpec);

/** @brief Utility helper class for field selector construction. */
class ELOG_API ELogFieldSelectorConstructor {
public:
    /**
     * @brief Constructs a field selector.
     * @param fieldSpec The field specification.
     * @return ELogFieldSelector* The resulting field selector, or null if failed.
     */
    virtual ELogFieldSelector* constructFieldSelector(const ELogFieldSpec& fieldSpec) = 0;

    /** @brief Installs field selector type id (for internal use only). */
    inline void setTypeId(uint32_t typeId) { m_typeId = typeId; }

    /** @brief Retrieves the field selector type id (for internal use only). */
    inline uint32_t getTypeId() const { return m_typeId; }

protected:
    /** @brief Constructor. */
    ELogFieldSelectorConstructor(const char* name) : m_typeId(0) {
        registerFieldSelectorConstructor(name, this);
    }

private:
    uint32_t m_typeId;
};

/** @def Utility macro for declaring field selector factory method registration. */
#define ELOG_DECLARE_FIELD_SELECTOR(FieldSelectorType, Name)                                    \
private:                                                                                        \
    class ELOG_API FieldSelectorType##Constructor : public elog::ELogFieldSelectorConstructor { \
    public:                                                                                     \
        FieldSelectorType##Constructor() : elog::ELogFieldSelectorConstructor(#Name) {}         \
        elog::ELogFieldSelector* constructFieldSelector(const ELogFieldSpec& fieldSpec) final { \
            return new (std::nothrow) FieldSelectorType(fieldSpec);                             \
        }                                                                                       \
    };                                                                                          \
    static FieldSelectorType##Constructor sConstructor;                                         \
                                                                                                \
public:                                                                                         \
    static uint32_t getTypeId() { return sConstructor.getTypeId(); }

/** @def Utility macro for implementing field selector factory method registration. */
#define ELOG_IMPLEMENT_FIELD_SELECTOR(FieldSelectorType) \
    FieldSelectorType::FieldSelectorType##Constructor FieldSelectorType::sConstructor;

/**
 * @brief Static text field selector, used for placing the strings between the fields in the log
 * format line specification string.
 */
class ELOG_API ELogStaticTextSelector : public ELogFieldSelector {
public:
    ELogStaticTextSelector(const char* text)
        : ELogFieldSelector(ELogFieldType::FT_TEXT), m_text(text) {}
    ~ELogStaticTextSelector() final {}

    void selectField(const ELogRecord& record, ELogFieldReceptor* receptor) final;

private:
    std::string m_text;
};

class ELOG_API ELogRecordIdSelector : public ELogFieldSelector {
public:
    ELogRecordIdSelector(const ELogFieldSpec& fieldSpec)
        : ELogFieldSelector(ELogFieldType::FT_INT, fieldSpec) {}
    ~ELogRecordIdSelector() final {}

    void selectField(const ELogRecord& record, ELogFieldReceptor* receptor) final;

private:
    ELOG_DECLARE_FIELD_SELECTOR(ELogRecordIdSelector, rid);
};

class ELOG_API ELogTimeSelector : public ELogFieldSelector {
public:
    ELogTimeSelector(const ELogFieldSpec& fieldSpec)
        : ELogFieldSelector(ELogFieldType::FT_DATETIME, fieldSpec) {}
    ~ELogTimeSelector() final {}

    void selectField(const ELogRecord& record, ELogFieldReceptor* receptor) final;

private:
    ELOG_DECLARE_FIELD_SELECTOR(ELogTimeSelector, time);
};

class ELOG_API ELogHostNameSelector : public ELogFieldSelector {
public:
    ELogHostNameSelector(const ELogFieldSpec& fieldSpec)
        : ELogFieldSelector(ELogFieldType::FT_TEXT, fieldSpec) {}
    ~ELogHostNameSelector() final {}

    void selectField(const ELogRecord& record, ELogFieldReceptor* receptor) final;

private:
    ELOG_DECLARE_FIELD_SELECTOR(ELogHostNameSelector, host);
};

class ELOG_API ELogUserNameSelector : public ELogFieldSelector {
public:
    ELogUserNameSelector(const ELogFieldSpec& fieldSpec)
        : ELogFieldSelector(ELogFieldType::FT_TEXT, fieldSpec) {}
    ~ELogUserNameSelector() final {}

    void selectField(const ELogRecord& record, ELogFieldReceptor* receptor) final;

private:
    ELOG_DECLARE_FIELD_SELECTOR(ELogUserNameSelector, user);
};

class ELOG_API ELogOsNameSelector : public ELogFieldSelector {
public:
    ELogOsNameSelector(const ELogFieldSpec& fieldSpec)
        : ELogFieldSelector(ELogFieldType::FT_TEXT, fieldSpec) {}
    ~ELogOsNameSelector() final {}

    void selectField(const ELogRecord& record, ELogFieldReceptor* receptor) final;

private:
    ELOG_DECLARE_FIELD_SELECTOR(ELogOsNameSelector, os);
};

class ELOG_API ELogOsVersionSelector : public ELogFieldSelector {
public:
    ELogOsVersionSelector(const ELogFieldSpec& fieldSpec)
        : ELogFieldSelector(ELogFieldType::FT_TEXT, fieldSpec) {}
    ~ELogOsVersionSelector() final {}

    void selectField(const ELogRecord& record, ELogFieldReceptor* receptor) final;

private:
    ELOG_DECLARE_FIELD_SELECTOR(ELogOsVersionSelector, os_ver);
};

class ELOG_API ELogAppNameSelector : public ELogFieldSelector {
public:
    ELogAppNameSelector(const ELogFieldSpec& fieldSpec)
        : ELogFieldSelector(ELogFieldType::FT_TEXT, fieldSpec) {}
    ~ELogAppNameSelector() final {}

    void selectField(const ELogRecord& record, ELogFieldReceptor* receptor) final;

private:
    ELOG_DECLARE_FIELD_SELECTOR(ELogAppNameSelector, app);
};

class ELOG_API ELogProgramNameSelector : public ELogFieldSelector {
public:
    ELogProgramNameSelector(const ELogFieldSpec& fieldSpec)
        : ELogFieldSelector(ELogFieldType::FT_TEXT, fieldSpec) {}
    ~ELogProgramNameSelector() final {}

    void selectField(const ELogRecord& record, ELogFieldReceptor* receptor) final;

private:
    ELOG_DECLARE_FIELD_SELECTOR(ELogProgramNameSelector, prog);
};

class ELOG_API ELogProcessIdSelector : public ELogFieldSelector {
public:
    ELogProcessIdSelector(const ELogFieldSpec& fieldSpec)
        : ELogFieldSelector(ELogFieldType::FT_INT, fieldSpec) {}
    ~ELogProcessIdSelector() final {}

    void selectField(const ELogRecord& record, ELogFieldReceptor* receptor) final;

private:
    ELOG_DECLARE_FIELD_SELECTOR(ELogProcessIdSelector, pid);
};

class ELOG_API ELogThreadIdSelector : public ELogFieldSelector {
public:
    ELogThreadIdSelector(const ELogFieldSpec& fieldSpec)
        : ELogFieldSelector(ELogFieldType::FT_INT, fieldSpec) {}
    ~ELogThreadIdSelector() final {}

    void selectField(const ELogRecord& record, ELogFieldReceptor* receptor) final;

private:
    ELOG_DECLARE_FIELD_SELECTOR(ELogThreadIdSelector, tid);
};

class ELOG_API ELogThreadNameSelector : public ELogFieldSelector {
public:
    ELogThreadNameSelector(const ELogFieldSpec& fieldSpec)
        : ELogFieldSelector(ELogFieldType::FT_TEXT, fieldSpec) {}
    ~ELogThreadNameSelector() final {}

    void selectField(const ELogRecord& record, ELogFieldReceptor* receptor) final;

private:
    ELOG_DECLARE_FIELD_SELECTOR(ELogThreadNameSelector, tname);
};

class ELOG_API ELogSourceSelector : public ELogFieldSelector {
public:
    ELogSourceSelector(const ELogFieldSpec& fieldSpec)
        : ELogFieldSelector(ELogFieldType::FT_TEXT, fieldSpec) {}
    ~ELogSourceSelector() final {}

    void selectField(const ELogRecord& record, ELogFieldReceptor* receptor) final;

private:
    ELOG_DECLARE_FIELD_SELECTOR(ELogSourceSelector, src);
};

class ELOG_API ELogModuleSelector : public ELogFieldSelector {
public:
    ELogModuleSelector(const ELogFieldSpec& fieldSpec)
        : ELogFieldSelector(ELogFieldType::FT_TEXT, fieldSpec) {}
    ~ELogModuleSelector() final {}

    void selectField(const ELogRecord& record, ELogFieldReceptor* receptor) final;

private:
    ELOG_DECLARE_FIELD_SELECTOR(ELogModuleSelector, mod);
};

class ELOG_API ELogFileSelector : public ELogFieldSelector {
public:
    ELogFileSelector(const ELogFieldSpec& fieldSpec)
        : ELogFieldSelector(ELogFieldType::FT_TEXT, fieldSpec) {}
    ~ELogFileSelector() final {}

    void selectField(const ELogRecord& record, ELogFieldReceptor* receptor) final;

private:
    ELOG_DECLARE_FIELD_SELECTOR(ELogFileSelector, file);
};

class ELOG_API ELogLineSelector : public ELogFieldSelector {
public:
    ELogLineSelector(const ELogFieldSpec& fieldSpec)
        : ELogFieldSelector(ELogFieldType::FT_INT, fieldSpec) {}
    ~ELogLineSelector() final {}

    void selectField(const ELogRecord& record, ELogFieldReceptor* receptor) final;

private:
    ELOG_DECLARE_FIELD_SELECTOR(ELogLineSelector, line);
};

class ELOG_API ELogFunctionSelector : public ELogFieldSelector {
public:
    ELogFunctionSelector(const ELogFieldSpec& fieldSpec)
        : ELogFieldSelector(ELogFieldType::FT_TEXT, fieldSpec) {}
    ~ELogFunctionSelector() final {}

    void selectField(const ELogRecord& record, ELogFieldReceptor* receptor) final;

private:
    ELOG_DECLARE_FIELD_SELECTOR(ELogFunctionSelector, func);
};

class ELOG_API ELogLevelSelector : public ELogFieldSelector {
public:
    ELogLevelSelector(const ELogFieldSpec& fieldSpec)
        : ELogFieldSelector(ELogFieldType::FT_TEXT, fieldSpec) {}
    ~ELogLevelSelector() final {}

    void selectField(const ELogRecord& record, ELogFieldReceptor* receptor) final;

private:
    ELOG_DECLARE_FIELD_SELECTOR(ELogLevelSelector, level);
};

class ELOG_API ELogMsgSelector : public ELogFieldSelector {
public:
    ELogMsgSelector(const ELogFieldSpec& fieldSpec)
        : ELogFieldSelector(ELogFieldType::FT_TEXT, fieldSpec) {}
    ~ELogMsgSelector() final {}

    void selectField(const ELogRecord& record, ELogFieldReceptor* receptor) final;

private:
    ELOG_DECLARE_FIELD_SELECTOR(ELogMsgSelector, msg);
};

}  // namespace elog

#endif  // __ELOG_FIELD_SELECTOR_H___