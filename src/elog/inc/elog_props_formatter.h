#ifndef __ELOG_PROPS_FORMATTER_H__
#define __ELOG_PROPS_FORMATTER_H__

#include "elog_formatter.h"

namespace elog {

class ELOG_API ELogPropsFormatter final : public ELogFormatter {
public:
    ELogPropsFormatter() : ELogFormatter(TYPE_NAME) {}
    ELogPropsFormatter(const ELogPropsFormatter&) = delete;
    ELogPropsFormatter(ELogPropsFormatter&&) = delete;
    ELogPropsFormatter& operator=(const ELogPropsFormatter&) = delete;
    ~ELogPropsFormatter() final {}

    static constexpr const char* TYPE_NAME = "props";

    bool parseProps(const std::string& props);

    inline void fillInProps(const ELogRecord& logRecord, elog::ELogFieldReceptor* receptor) {
        applyFieldSelectors(logRecord, receptor);
    }

    inline const std::string& getPropNameAt(uint32_t index) const { return m_propNames[index]; }

    inline uint32_t getPropCount() const { return (uint32_t)m_propNames.size(); }

    inline const std::vector<std::string>& getPropNames() const { return m_propNames; }

private:
    std::vector<std::string> m_propNames;

    ELOG_DECLARE_LOG_FORMATTER(ELogPropsFormatter, props)
};

}  // namespace elog

#endif  // __ELOG_PROPS_FORMATTER_H__