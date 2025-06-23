#include "elog_json_receptor.h"

#ifdef ELOG_ENABLE_JSON

#include "elog_common.h"
#include "elog_error.h"

namespace elog {

bool ELogJsonReceptor::prepareJsonMap(nlohmann::json& logAttributes,
                                      const std::vector<std::string>& propNames) {
    if (m_propValues.size() != propNames.size()) {
        ELOG_REPORT_ERROR(
            "Mismatching JSON property names and values (%u names, %u values) in JSON receptor",
            propNames.size(), m_propValues.size());
        return false;
    }
    for (uint32_t i = 0; i < m_propValues.size(); ++i) {
        logAttributes[propNames[i]] = m_propValues[i];
    }
    return true;
}

}  // namespace elog

#endif  // ELOG_ENABLE_JSON