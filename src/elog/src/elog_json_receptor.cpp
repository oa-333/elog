#include "elog_json_receptor.h"

#ifdef ELOG_ENABLE_JSON

#include "elog_common.h"
#include "elog_report.h"

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogJsonReceptor)

// NOTE: we disable the MSVC compiler warning c4866, which seems like a compiler limitation,
// not being able to evaluate from left to right (see discussion here:
// https://stackoverflow.com/questions/66185151/warning-c4866-raised-by-microsoft-visual-c)
#ifdef ELOG_MSVC
#pragma warning(push)
#pragma warning(disable : 4866)
#endif

bool ELogJsonReceptor::prepareJsonMap(nlohmann::json& logAttributes,
                                      const std::vector<std::string>& propNames) {
    if (m_propValues.size() != propNames.size()) {
        ELOG_REPORT_MODERATE_ERROR_DEFAULT(
            "Mismatching JSON property names and values (%u names, %u values) in JSON receptor",
            propNames.size(), m_propValues.size());
        return false;
    }
    for (uint32_t i = 0; i < m_propValues.size(); ++i) {
        // NOTE: MSVC compiler issues warning c4866 here
        logAttributes[propNames[i]] = m_propValues[i];
    }
    return true;
}

#ifdef ELOG_MSVC
#pragma warning(pop)
#endif

}  // namespace elog

#endif  // ELOG_ENABLE_JSON