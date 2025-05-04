#ifndef __ELOG_FIELD_SELECTOR__
#define __ELOG_FIELD_SELECTOR__

#include <sstream>

#include "elog_def.h"
#include "elog_field_receptor.h"
#include "elog_record.h"

namespace elog {

/** @brief Initialize all field selectors (for internal use only). */
extern ELOG_API bool initFieldSelectors();

/** @brief Retrieves program name (for internal use only). */
extern ELOG_API const char* getProgramName();

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

protected:
    ELogFieldSelector(int justify = 0) : m_justify(justify) {}

    int m_justify;
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
 * @brief Utility helper for constructing message from an input stream given a message id.
 * @param name The field selector identifier.
 * @param justify The field justification.
 * @return ELogFieldSelector* The resulting field selector, or null if failed.
 */
extern ELOG_API ELogFieldSelector* constructFieldSelector(const char* name, int justify);

/** @brief Utility helper class for message construction. */
class ELOG_API ELogFieldSelectorConstructor {
public:
    /**
     * @brief Constructs a field selector.
     * @param justify The field justification.
     * @return ELogFieldSelector* The resulting field selector, or null if failed.
     */
    virtual ELogFieldSelector* constructFieldSelector(int justify) = 0;

protected:
    /** @brief Constructor. */
    ELogFieldSelectorConstructor(const char* name) { registerFieldSelectorConstructor(name, this); }
};

/** @def Utility macro for declaring field selector factory method registration. */
#define ELOG_DECLARE_FIELD_SELECTOR(FieldSelectorType, Name)                            \
    class FieldSelectorType##Constructor : public elog::ELogFieldSelectorConstructor {  \
    public:                                                                             \
        FieldSelectorType##Constructor() : elog::ELogFieldSelectorConstructor(#Name) {} \
        elog::ELogFieldSelector* constructFieldSelector(int justify) final {            \
            return new (std::nothrow) FieldSelectorType(justify);                       \
        }                                                                               \
    };                                                                                  \
    static FieldSelectorType##Constructor sConstructor;

/** @def Utility macro for implementing field selector factory method registration. */
#define ELOG_IMPLEMENT_FIELD_SELECTOR(FieldSelectorType) \
    FieldSelectorType::FieldSelectorType##Constructor FieldSelectorType::sConstructor;

/**
 * @brief Static text field selector, used for placing the strings between the fields in the log
 * format line specification string.
 */
class ELOG_API ELogStaticTextSelector : public ELogFieldSelector {
public:
    ELogStaticTextSelector(const char* text) : m_text(text) {}
    ~ELogStaticTextSelector() final {}

    void selectField(const ELogRecord& record, ELogFieldReceptor* receptor) final;

private:
    std::string m_text;
};

class ELOG_API ELogRecordIdSelector : public ELogFieldSelector {
public:
    ELogRecordIdSelector(int justify) : ELogFieldSelector(justify) {}
    ~ELogRecordIdSelector() final {}

    void selectField(const ELogRecord& record, ELogFieldReceptor* receptor) final;

private:
    ELOG_DECLARE_FIELD_SELECTOR(ELogRecordIdSelector, rid);
};

class ELOG_API ELogTimeSelector : public ELogFieldSelector {
public:
    ELogTimeSelector(int justify) : ELogFieldSelector(justify) {}
    ~ELogTimeSelector() final {}

    void selectField(const ELogRecord& record, ELogFieldReceptor* receptor) final;

private:
    ELOG_DECLARE_FIELD_SELECTOR(ELogTimeSelector, time);
};

class ELOG_API ELogHostNameSelector : public ELogFieldSelector {
public:
    ELogHostNameSelector(int justify) : ELogFieldSelector(justify) {}
    ~ELogHostNameSelector() final {}

    void selectField(const ELogRecord& record, ELogFieldReceptor* receptor) final;

private:
    ELOG_DECLARE_FIELD_SELECTOR(ELogHostNameSelector, host);
};

class ELOG_API ELogUserNameSelector : public ELogFieldSelector {
public:
    ELogUserNameSelector(int justify) : ELogFieldSelector(justify) {}
    ~ELogUserNameSelector() final {}

    void selectField(const ELogRecord& record, ELogFieldReceptor* receptor) final;

private:
    ELOG_DECLARE_FIELD_SELECTOR(ELogUserNameSelector, user);
};

class ELOG_API ELogProgramNameSelector : public ELogFieldSelector {
public:
    ELogProgramNameSelector(int justify) : ELogFieldSelector(justify) {}
    ~ELogProgramNameSelector() final {}

    void selectField(const ELogRecord& record, ELogFieldReceptor* receptor) final;

private:
    ELOG_DECLARE_FIELD_SELECTOR(ELogProgramNameSelector, prog);
};

class ELOG_API ELogProcessIdSelector : public ELogFieldSelector {
public:
    ELogProcessIdSelector(int justify) : ELogFieldSelector(justify) {}
    ~ELogProcessIdSelector() final {}

    void selectField(const ELogRecord& record, ELogFieldReceptor* receptor) final;

private:
    ELOG_DECLARE_FIELD_SELECTOR(ELogProcessIdSelector, pid);
};

class ELOG_API ELogThreadIdSelector : public ELogFieldSelector {
public:
    ELogThreadIdSelector(int justify) : ELogFieldSelector(justify) {}
    ~ELogThreadIdSelector() final {}

    void selectField(const ELogRecord& record, ELogFieldReceptor* receptor) final;

private:
    ELOG_DECLARE_FIELD_SELECTOR(ELogThreadIdSelector, tid);
};

class ELOG_API ELogSourceSelector : public ELogFieldSelector {
public:
    ELogSourceSelector(int justify) : ELogFieldSelector(justify) {}
    ~ELogSourceSelector() final {}

    void selectField(const ELogRecord& record, ELogFieldReceptor* receptor) final;

private:
    ELOG_DECLARE_FIELD_SELECTOR(ELogSourceSelector, src);
};

class ELOG_API ELogModuleSelector : public ELogFieldSelector {
public:
    ELogModuleSelector(int justify) : ELogFieldSelector(justify) {}
    ~ELogModuleSelector() final {}

    void selectField(const ELogRecord& record, ELogFieldReceptor* receptor) final;

private:
    ELOG_DECLARE_FIELD_SELECTOR(ELogModuleSelector, mod);
};

class ELOG_API ELogLevelSelector : public ELogFieldSelector {
public:
    ELogLevelSelector(int justify) : ELogFieldSelector(justify) {}
    ~ELogLevelSelector() final {}

    void selectField(const ELogRecord& record, ELogFieldReceptor* receptor) final;

private:
    ELOG_DECLARE_FIELD_SELECTOR(ELogLevelSelector, level);
};

class ELOG_API ELogMsgSelector : public ELogFieldSelector {
public:
    ELogMsgSelector(int justify) : ELogFieldSelector(justify) {}
    ~ELogMsgSelector() final {}

    void selectField(const ELogRecord& record, ELogFieldReceptor* receptor) final;

private:
    ELOG_DECLARE_FIELD_SELECTOR(ELogMsgSelector, msg);
};

}  // namespace elog

#endif  // __ELOG_FIELD_SELECTOR__