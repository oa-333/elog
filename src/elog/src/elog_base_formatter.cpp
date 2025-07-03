#include "elog_base_formatter.h"

#include <cstring>

#include "elog_common.h"
#include "elog_error.h"
#include "elog_string_stream_receptor.h"

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
            if (!handleText(formatSpec.substr(prevPos, pos - prevPos))) {
                return false;
            }
        }
        std::string::size_type closePos = formatSpec.find("}", pos);
        if (closePos == std::string::npos) {
            ELOG_REPORT_ERROR("Missing closing brace in log line format specification");
            return false;
        }
        std::string fieldSpecStr = formatSpec.substr(pos + 2, closePos - pos - 2);
        ELogFieldSpec fieldSpec;
        if (!parseFieldSpec(fieldSpecStr, fieldSpec)) {
            return false;
        }
        if (!handleField(fieldSpec)) {
            return false;
        }

        prevPos = closePos + 1;
        pos = formatSpec.find("${", prevPos);
    }
    if (prevPos != std::string::npos && prevPos < formatSpec.length()) {
        if (!handleText(formatSpec.substr(prevPos).c_str())) {
            return false;
        }
    }
    return true;
}

// TODO: support log level color configuration
// we specify VGA 216 color like this:
// {...:fg-color=vga#RGB}, where each RGB component cannot exceed 5 bits (<= 0x1F)
// {...:bg-color:grey#value}, where value <= 0x17 (0-23 decimal)
// gary keyword is also acceptable
bool ELogBaseFormatter::parseFieldSpec(const std::string& fieldSpecStr, ELogFieldSpec& fieldSpec) {
    // all functionality now delegated to ELogFieldSpec due to future needs (per-log-level text
    // formatting)
    return fieldSpec.parse(fieldSpecStr);
}

bool ELogBaseFormatter::handleText(const std::string& text) {
    // by default we add a static text field selector
    ELogFieldSelector* fieldSelector = new (std::nothrow) ELogStaticTextSelector(text.c_str());
    if (fieldSelector == nullptr) {
        ELOG_REPORT_ERROR("Failed to allocate field selector for static text '%s', out of memory",
                          text.c_str());
        return false;
    }
    m_fieldSelectors.push_back(fieldSelector);
    return true;
}

bool ELogBaseFormatter::handleField(const ELogFieldSpec& fieldSpec) {
    ELogFieldSelector* fieldSelector = constructFieldSelector(fieldSpec);
    if (fieldSelector == nullptr) {
        return false;
    }
    m_fieldSelectors.push_back(fieldSelector);
    return true;
}

}  // namespace elog
