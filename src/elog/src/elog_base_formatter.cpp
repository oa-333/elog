#include "elog_base_formatter.h"

#include <cstring>

#include "elog_string_stream_receptor.h"
#include "elog_system.h"

namespace elog {

ELogBaseFormatter::~ELogBaseFormatter() {
    for (ELogFieldSelector* fieldSelector : m_fieldSelectors) {
        delete fieldSelector;
    }
    m_fieldSelectors.clear();
}

void ELogBaseFormatter::applyFieldSelectors(const ELogRecord& logRecord,
                                            ELogFieldReceptor* receptor) {
    for (ELogFieldSelector* fieldSelector : m_fieldSelectors) {
        fieldSelector->selectField(logRecord, receptor);
    }
}

bool ELogBaseFormatter::parseFormatSpec(const std::string& formatSpec) {
    // repeatedly search for "${"
    std::string::size_type prevPos = 0;
    std::string::size_type pos = formatSpec.find("${");
    while (pos != std::string::npos) {
        if (pos > prevPos) {
            if (!handleText(formatSpec.substr(prevPos, pos - prevPos).c_str())) {
                return false;
            }
        }
        std::string::size_type closePos = formatSpec.find("}", pos);
        if (closePos == std::string::npos) {
            ELogSystem::reportError("Missing closing brace in log line format specification");
            return false;
        }
        std::string fieldName = formatSpec.substr(pos + 2, closePos - pos - 2);

        int justify = 0;
        std::string::size_type colonPos = fieldName.find(':');
        if (colonPos != std::string::npos) {
            try {
                justify = std::stoi(fieldName.substr(colonPos + 1).c_str());
            } catch (std::exception& e) {
                ELOG_WARN(
                    "Invalid justification number encountered, while parsing field selector %s",
                    fieldName.c_str());
            }
            fieldName = fieldName.substr(0, colonPos);
        }
        if (!handleField(fieldName.c_str(), justify)) {
            return false;
        }

        prevPos = closePos + 1;
        pos = formatSpec.find("${", prevPos);
    }
    if (prevPos != std::string::npos) {
        if (!handleText(formatSpec.substr(prevPos).c_str())) {
            return false;
        }
    }
    return true;
}

bool ELogBaseFormatter::handleText(const std::string& text) {
    // by default we add a static text field selector
    ELogFieldSelector* fieldSelector = new (std::nothrow) ELogStaticTextSelector(text.c_str());
    if (fieldSelector == nullptr) {
        ELogSystem::reportError(
            "Failed to allocate field selector for static text '%s', out of memory", text.c_str());
        return false;
    }
    m_fieldSelectors.push_back(fieldSelector);
    return true;
}

bool ELogBaseFormatter::handleField(const char* fieldName, int justify) {
    ELogFieldSelector* fieldSelector = constructFieldSelector(fieldName, justify);
    if (fieldSelector == nullptr) {
        return false;
    }
    m_fieldSelectors.push_back(fieldSelector);
    return true;
}

}  // namespace elog
