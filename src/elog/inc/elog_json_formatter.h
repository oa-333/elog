#ifndef __ELOG_JSON_FORMATTER_H__
#define __ELOG_JSON_FORMATTER_H__

#ifdef ELOG_ENABLE_JSON

#include <nlohmann/json.hpp>

#include "elog_def.h"
#include "elog_props_formatter.h"

namespace elog {

/**
 * @class A JSON formatter, which takes input as json map, and parses property names and values as
 * field selectors.
 */
class ELOG_API ELogJsonFormatter final : public ELogFormatter {
public:
    ELogJsonFormatter() : ELogFormatter(TYPE_NAME) {}
    ELogJsonFormatter(const ELogJsonFormatter&) = delete;
    ELogJsonFormatter(ELogJsonFormatter&&) = delete;
    ELogJsonFormatter& operator=(const ELogJsonFormatter&) = delete;
    ~ELogJsonFormatter() final {}

    static constexpr const char* TYPE_NAME = "json";

    bool parseJson(const std::string& jsonStr);

    inline void fillInProps(const ELogRecord& logRecord, elog::ELogFieldReceptor* receptor) {
        applyFieldSelectors(logRecord, receptor);
    }

    inline const std::string& getPropNameAt(uint32_t index) const { return m_propNames[index]; }

    inline uint32_t getPropCount() const { return (uint32_t)m_propNames.size(); }

    inline const std::vector<std::string>& getPropNames() const { return m_propNames; }

private:
    nlohmann::json m_jsonField;
    std::vector<std::string> m_propNames;

    ELOG_DECLARE_LOG_FORMATTER(ELogJsonFormatter, json)
};

}  // namespace elog

#endif  // ELOG_ENABLE_JSON

#endif  // __ELOG_JSON_FORMATTER_H__