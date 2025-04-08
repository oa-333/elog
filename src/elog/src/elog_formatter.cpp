#include "elog_formatter.h"

#include <cstring>

#include "elog_system.h"

namespace elog {

ELogFormatter::~ELogFormatter() {
    for (ELogFieldSelector* fieldSelector : m_fieldSelectors) {
        delete fieldSelector;
    }
    m_fieldSelectors.clear();
}

void ELogFormatter::formatLogMsg(const ELogRecord& logRecord, std::string& logMsg) {
    std::stringstream msgStream;
    for (ELogFieldSelector* fieldSelector : m_fieldSelectors) {
        fieldSelector->selectField(logRecord, msgStream);
    }
    logMsg = msgStream.str();
}

bool ELogFormatter::parseFormatSpec(const std::string& formatSpec) {
    // repeatedly search for "${"
    std::string::size_type prevPos = 0;
    std::string::size_type pos = formatSpec.find("${");
    while (pos != std::string::npos) {
        if (pos > prevPos) {
            m_fieldSelectors.push_back(new (std::nothrow) ELogStaticTextSelector(
                formatSpec.substr(prevPos, pos - prevPos).c_str()));
        }
        std::string::size_type closePos = formatSpec.find("}", pos);
        if (closePos == std::string::npos) {
            ELOG_ERROR("Missing closing brace in log line format specification");
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
        ELogFieldSelector* fieldSelector = createFieldSelector(fieldName.c_str(), justify);
        if (fieldSelector == nullptr) {
            return false;
        }
        m_fieldSelectors.push_back(fieldSelector);

        prevPos = closePos + 1;
        pos = formatSpec.find("${", prevPos);
    }
    return true;
}

ELogFieldSelector* ELogFormatter::createFieldSelector(const char* fieldName, int justify) {
    // following special tokens can be sued in configuration:
    // ${rid} ${time} ${host} ${user} ${pid} ${tid} ${level} ${sid} ${msg}
    ELogFieldSelector* selector = nullptr;
    if (strcmp(fieldName, "rid") == 0) {
        selector = new (std::nothrow) ELogRecordIdSelector(justify);
    } else if (strcmp(fieldName, "time") == 0) {
        selector = new (std::nothrow) ELogTimeSelector(justify);
    } else if (strcmp(fieldName, "host") == 0) {
        selector = new (std::nothrow) ELogHostNameSelector(justify);
    } else if (strcmp(fieldName, "user") == 0) {
        selector = new (std::nothrow) ELogUserNameSelector(justify);
    } else if (strcmp(fieldName, "pid") == 0) {
        selector = new (std::nothrow) ELogProcessIdSelector(justify);
    } else if (strcmp(fieldName, "tid") == 0) {
        selector = new (std::nothrow) ELogThreadIdSelector(justify);
    } else if (strcmp(fieldName, "level") == 0) {
        selector = new (std::nothrow) ELogLevelSelector(justify);
    } else if (strcmp(fieldName, "src") == 0) {
        selector = new (std::nothrow) ELogSourceSelector(justify);
    } else if (strcmp(fieldName, "msg") == 0) {
        selector = new (std::nothrow) ELogMsgSelector(justify);
    } else {
        ELOG_ERROR("Invalid field selector: %s", fieldName);
        return nullptr;
    }

    if (selector == nullptr) {
        ELOG_ERROR("Failed to create field selector, out of memory");
    }
    return selector;
}

}  // namespace elog
