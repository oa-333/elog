#ifndef __ELOG_FIELD_SELECTOR__
#define __ELOG_FIELD_SELECTOR__

#include <sstream>

#include "elog_def.h"
#include "elog_record.h"

namespace elog {

extern DLL_EXPORT void initFieldSelectors();

/**
 * @brief Parent interface for all field selectors. Custom selectors may be added by deriving from
 * this interface and deriving from @ref ELogFormatter, so that the field can be parsed by
 * overriding the virtual function @ref createFieldSelector().
 */
class DLL_EXPORT ELogFieldSelector {
public:
    virtual ~ELogFieldSelector() {}

    /**
     * @brief Selects a field from the log record (or from external source) and appends it to the
     * message stream.
     * @param record The log record from which a field is to be selected.
     * @param msgStream The message stream receiveing the log record in string form.
     */
    virtual void selectField(const ELogRecord& record, std::stringstream& msgStream) = 0;

protected:
    ELogFieldSelector(int justify = 0) : m_justify(justify) {}

    void applyJustify(std::stringstream& msgStream);

private:
    int m_justify;
};

/**
 * @brief Static text field selector, used for placing the strings between the fields in the log
 * format line specification string.
 */
class DLL_EXPORT ELogStaticTextSelector : public ELogFieldSelector {
public:
    ELogStaticTextSelector(const char* text) : m_text(text) {}
    ~ELogStaticTextSelector() final {}

    void selectField(const ELogRecord& record, std::stringstream& msgStream) final;

private:
    std::string m_text;
};

class DLL_EXPORT ELogRecordIdSelector : public ELogFieldSelector {
public:
    ELogRecordIdSelector(int justify) : ELogFieldSelector(justify) {}
    ~ELogRecordIdSelector() final {}

    void selectField(const ELogRecord& record, std::stringstream& msgStream) final;

private:
    int m_justify;
};

class DLL_EXPORT ELogTimeSelector : public ELogFieldSelector {
public:
    ELogTimeSelector(int justify) : ELogFieldSelector(justify) {}
    ~ELogTimeSelector() final {}

    void selectField(const ELogRecord& record, std::stringstream& msgStream) final;
};

class DLL_EXPORT ELogHostNameSelector : public ELogFieldSelector {
public:
    ELogHostNameSelector(int justify) : ELogFieldSelector(justify) {}
    ~ELogHostNameSelector() final {}

    void selectField(const ELogRecord& record, std::stringstream& msgStream) final;
};

class DLL_EXPORT ELogUserNameSelector : public ELogFieldSelector {
public:
    ELogUserNameSelector(int justify) : ELogFieldSelector(justify) {}
    ~ELogUserNameSelector() final {}

    void selectField(const ELogRecord& record, std::stringstream& msgStream) final;
};

class DLL_EXPORT ELogProcessIdSelector : public ELogFieldSelector {
public:
    ELogProcessIdSelector(int justify) : ELogFieldSelector(justify) {}
    ~ELogProcessIdSelector() final {}

    void selectField(const ELogRecord& record, std::stringstream& msgStream) final;
};

class DLL_EXPORT ELogThreadIdSelector : public ELogFieldSelector {
public:
    ELogThreadIdSelector(int justify) : ELogFieldSelector(justify) {}
    ~ELogThreadIdSelector() final {}

    void selectField(const ELogRecord& record, std::stringstream& msgStream) final;
};

class DLL_EXPORT ELogSourceSelector : public ELogFieldSelector {
public:
    ELogSourceSelector(int justify) : ELogFieldSelector(justify) {}
    ~ELogSourceSelector() final {}

    void selectField(const ELogRecord& record, std::stringstream& msgStream) final;
};

class DLL_EXPORT ELogModuleSelector : public ELogFieldSelector {
public:
    ELogModuleSelector(int justify) : ELogFieldSelector(justify) {}
    ~ELogModuleSelector() final {}

    void selectField(const ELogRecord& record, std::stringstream& msgStream) final;
};

class DLL_EXPORT ELogLevelSelector : public ELogFieldSelector {
public:
    ELogLevelSelector(int justify) : ELogFieldSelector(justify) {}
    ~ELogLevelSelector() final {}

    void selectField(const ELogRecord& record, std::stringstream& msgStream) final;
};

class DLL_EXPORT ELogMsgSelector : public ELogFieldSelector {
public:
    ELogMsgSelector(int justify) : ELogFieldSelector(justify) {}
    ~ELogMsgSelector() final {}

    void selectField(const ELogRecord& record, std::stringstream& msgStream) final;
};

}  // namespace elog

#endif  // __ELOG_FIELD_SELECTOR__