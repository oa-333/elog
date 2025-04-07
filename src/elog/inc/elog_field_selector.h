#ifndef __ELOG_FIELD_SELECTOR__
#define __ELOG_FIELD_SELECTOR__

#include <sstream>

#include "elog_record.h"

namespace elog {

class ELogFieldSelector {
public:
    virtual ~ELogFieldSelector() {}

    virtual void selectField(const ELogRecord& record, std::stringstream& msgStream) = 0;

protected:
    ELogFieldSelector() {}
};

class ELogStaticTextSelector : public ELogFieldSelector {
public:
    ELogStaticTextSelector(const char* text) { m_text = text; }
    ~ELogStaticTextSelector() final {}

    void selectField(const ELogRecord& record, std::stringstream& msgStream) final;

private:
    std::string m_text;
};

class ELogRecordIdSelector : public ELogFieldSelector {
public:
    ELogRecordIdSelector() {}
    ~ELogRecordIdSelector() final {}

    void selectField(const ELogRecord& record, std::stringstream& msgStream) final;
};

class ELogTimeSelector : public ELogFieldSelector {
public:
    ELogTimeSelector() {}
    ~ELogTimeSelector() final {}

    void selectField(const ELogRecord& record, std::stringstream& msgStream) final;
};

class ELogHostNameSelector : public ELogFieldSelector {
public:
    ELogHostNameSelector() {}
    ~ELogHostNameSelector() final {}

    void selectField(const ELogRecord& record, std::stringstream& msgStream) final;
};

class ELogUserNameSelector : public ELogFieldSelector {
public:
    ELogUserNameSelector() {}
    ~ELogUserNameSelector() final {}

    void selectField(const ELogRecord& record, std::stringstream& msgStream) final;
};

class ELogProcessIdSelector : public ELogFieldSelector {
public:
    ELogProcessIdSelector() {}
    ~ELogProcessIdSelector() final {}

    void selectField(const ELogRecord& record, std::stringstream& msgStream) final;
};

class ELogThreadIdSelector : public ELogFieldSelector {
public:
    ELogThreadIdSelector() {}
    ~ELogThreadIdSelector() final {}

    void selectField(const ELogRecord& record, std::stringstream& msgStream) final;
};

class ELogSourceSelector : public ELogFieldSelector {
public:
    ELogSourceSelector() {}
    ~ELogSourceSelector() final {}

    void selectField(const ELogRecord& record, std::stringstream& msgStream) final;
};

class ELogLevelSelector : public ELogFieldSelector {
public:
    ELogLevelSelector() {}
    ~ELogLevelSelector() final {}

    void selectField(const ELogRecord& record, std::stringstream& msgStream) final;
};

class ELogMsgSelector : public ELogFieldSelector {
public:
    ELogMsgSelector() {}
    ~ELogMsgSelector() final {}

    void selectField(const ELogRecord& record, std::stringstream& msgStream) final;
};

}  // namespace elog

#endif  // __ELOG_FIELD_SELECTOR__