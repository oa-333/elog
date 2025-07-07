#ifndef __ELOG_PROPS_FORMATTER_H__
#define __ELOG_PROPS_FORMATTER_H__

#include "elog_base_formatter.h"

namespace elog {

class ELOG_API ELogPropsFormatter : public ELogBaseFormatter {
public:
    ELogPropsFormatter() {}
    ELogPropsFormatter(const ELogPropsFormatter&) = delete;
    ELogPropsFormatter(ELogPropsFormatter&&) = delete;
    ~ELogPropsFormatter() final {}

    bool parseProps(const std::string& props);

    inline void fillInProps(const ELogRecord& logRecord, elog::ELogFieldReceptor* receptor) {
        applyFieldSelectors(logRecord, receptor);
    }

    inline const std::string& getPropNameAt(uint32_t index) const { return m_propNames[index]; }

    inline uint32_t getPropCount() const { return m_propNames.size(); }

    inline const std::vector<std::string>& getPropNames() const { return m_propNames; }

private:
    std::vector<std::string> m_propNames;
};

}  // namespace elog

#endif  // __ELOG_PROPS_FORMATTER_H__