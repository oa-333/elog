#include "elog_formatter.h"

#include <cstring>

#include "elog_buffer_receptor.h"
#include "elog_common.h"
#include "elog_config_loader.h"
#include "elog_filter.h"
#include "elog_report.h"
#include "elog_string_receptor.h"
#include "elog_string_stream_receptor.h"

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogFormatter)

/** @def The maximum number of log formatter types that can be defined in the system. */
#define ELOG_MAX_LOG_FORMATTER_COUNT 100

// implement log formatter factory by name with static registration
struct ELogFormatterNameConstructor {
    const char* m_name;
    ELogFormatterConstructor* m_ctor;
};

static ELogFormatterNameConstructor sLogFormatterConstructors[ELOG_MAX_LOG_FORMATTER_COUNT] = {};
static uint32_t sLogFormatterConstructorsCount = 0;

typedef std::unordered_map<std::string, ELogFormatterConstructor*> ELogFormatterConstructorMap;

static ELogFormatterConstructorMap sLogFormatterConstructorMap;

void registerLogFormatterConstructor(const char* name, ELogFormatterConstructor* constructor) {
    // due to c runtime issues on MinGW we delay access to unordered map
    if (sLogFormatterConstructorsCount >= ELOG_MAX_LOG_FORMATTER_COUNT) {
        ELOG_REPORT_ERROR("Cannot register log formatter constructor, no space: %s", name);
        exit(1);
    } else {
        sLogFormatterConstructors[sLogFormatterConstructorsCount++] = {name, constructor};
    }
}

static bool applyLogFormatterConstructorRegistration() {
    for (uint32_t i = 0; i < sLogFormatterConstructorsCount; ++i) {
        ELogFormatterNameConstructor& nameCtorPair = sLogFormatterConstructors[i];
        if (!sLogFormatterConstructorMap
                 .insert(ELogFormatterConstructorMap::value_type(nameCtorPair.m_name,
                                                                 nameCtorPair.m_ctor))
                 .second) {
            ELOG_REPORT_ERROR("Duplicate log formatter identifier: %s", nameCtorPair.m_name);
            return false;
        }
    }
    return true;
}

bool initLogFormatters() { return applyLogFormatterConstructorRegistration(); }

void termLogFormatters() { sLogFormatterConstructorMap.clear(); }

ELogFormatter* constructLogFormatter(const char* name) {
    ELogFormatterConstructorMap::iterator itr = sLogFormatterConstructorMap.find(name);
    if (itr == sLogFormatterConstructorMap.end()) {
        ELOG_REPORT_ERROR("Invalid log formatter %s: not found", name);
        return nullptr;
    }

    ELogFormatterConstructor* constructor = itr->second;
    ELogFormatter* logFormatter = constructor->constructFormatter();
    if (logFormatter == nullptr) {
        ELOG_REPORT_ERROR("Failed to createlog formatter, out of memory");
    }
    return logFormatter;
}

ELogFormatter::~ELogFormatter() {
    for (ELogFieldSelector* fieldSelector : m_fieldSelectors) {
        delete fieldSelector;
    }
    m_fieldSelectors.clear();
}
void ELogFormatter::formatLogMsg(const ELogRecord& logRecord, std::string& logMsg) {
    // unlike the string stream receptor, the string receptor formats directly the resulting log
    // message string, and so we save one or two string copies
    ELogStringReceptor receptor(logMsg);
    applyFieldSelectors(logRecord, &receptor);
}

void ELogFormatter::formatLogBuffer(const ELogRecord& logRecord, ELogBuffer& logBuffer) {
    ELogBufferReceptor receptor(logBuffer);
    applyFieldSelectors(logRecord, &receptor);
}

void ELogFormatter::applyFieldSelectors(const ELogRecord& logRecord, ELogFieldReceptor* receptor) {
    for (ELogFieldSelector* fieldSelector : m_fieldSelectors) {
        fieldSelector->selectField(logRecord, receptor);
    }
}

bool ELogFormatter::parseFormatSpec(const std::string& formatSpec) {
    // repeatedly search for "${"
    std::string::size_type prevPos = 0;
    std::string::size_type pos = formatSpec.find("${");
    while (pos != std::string::npos) {
        if (pos > prevPos) {
            if (!handleText(formatSpec.substr(prevPos, pos - prevPos))) {
                return false;
            }
        }
        // since conditional formatting may contain nested selectors, the closing brace should
        // actually be computed instead of searched
        std::string::size_type closePos = std::string::npos;
        if (!getFieldCloseBrace(formatSpec, pos, closePos)) {
            return false;
        }
        std::string fieldSpecStr = formatSpec.substr(pos + 2, closePos - pos - 2);
        if (!parseFieldSpec(fieldSpecStr)) {
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
bool ELogFormatter::parseFieldSpec(const std::string& fieldSpecStr) {
    // all functionality now delegated to ELogFieldSpec due to future needs (per-log-level text
    // formatting), except for conditional formatting:
    //
    // ${if: (filter-pred): ${name:<true format>} [: ${name:< false format>}] }
    // ${switch: (expr): ${case: (expr) : ${fmt:<format>}}, ..., ${default:${fmt: <format>}} }
    // ${expr-switch: ${case: (filter-pred) : ${fmt:<format>}}, ..., ${default:${fmt: <format>}} }
    // the filter predicate is ELogFilter*.
    // the expr is ELogFieldSelector that yields int, string, level or time.
    // it could specify a field reference, with ${level} for instance, or it could specify a
    // constant as an integer, string, level or time. A qualified constant can be given with the
    // field reference: ${const-int:<value>}, ${const-string:<value>}, ${const-level:<value>},
    // and ${const-time:<value>} - so the expression is actually a field selector, and the
    // expressions result type MUST match.
    // the vswitch is actually a full-blown if else.
    std::string::size_type colonPos = fieldSpecStr.find(':');
    std::string name = fieldSpecStr.substr(0, colonPos);
    if (name.compare("if") == 0) {
        return parseCondField(fieldSpecStr);
    } else if (name.compare("switch") == 0) {
        return parseSwitchField(fieldSpecStr);
    } else if (name.compare("expr-switch") == 0) {
        return parseExprSwitchField(fieldSpecStr);
    } else {
        return parseSimpleField(fieldSpecStr);
    }
}

bool ELogFormatter::handleText(const std::string& text) {
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

bool ELogFormatter::handleField(const ELogFieldSpec& fieldSpec) {
    ELogFieldSelector* fieldSelector = constructFieldSelector(fieldSpec);
    if (fieldSelector == nullptr) {
        return false;
    }
    m_fieldSelectors.push_back(fieldSelector);
    return true;
}

bool ELogFormatter::parseValue(const std::string& value) {
    // check if this is a field reference
    if (value.find("${") == 0) {
        // verify field reference syntax
        if (value.back() != '}') {
            ELOG_REPORT_ERROR("Invalid field specification %s, missing closing curly brace",
                              value.c_str());
            return false;
        }

        // extract field spec string and parse
        // NOTE: the call to parseFieldSpec() already triggers a call to handleField()
        std::string valueStr = value.substr(2, value.size() - 3);
        if (!parseFieldSpec(valueStr)) {
            ELOG_REPORT_ERROR("Failed to parse field value '%s'", valueStr.c_str());
            return false;
        }
    } else {
        // otherwise, this is plain static text
        ELOG_REPORT_TRACE("Extracted static text value: %s", value.c_str());
        if (!handleText(value)) {
            return false;
        }
    }
    return true;
}

// TODO: should this entire loading/parsing code be moved to config loader/parser?

bool ELogFormatter::getFieldCloseBrace(const std::string& formatSpec, std::string::size_type from,
                                       std::string::size_type& closePos) {
    // it is expected to have the first char as open brace
    int count = 0;
    bool countChanged = false;
    while (from < formatSpec.length()) {
        char c = formatSpec[from];
        if (c == '{') {
            ++count;
            countChanged = true;
        } else if (c == '}') {
            --count;
            countChanged = true;
        }
        if (count < 0) {
            ELOG_REPORT_ERROR(
                "Invalid format specification, nested expression syntax error at position %zu: %s",
                from, formatSpec.c_str());
            return false;
        }
        if (count == 0 && countChanged) {
            closePos = from;
            return true;
        }
        ++from;
    }

    ELOG_REPORT_ERROR("Invalid format specification, nested expression missing close brace(s): %s",
                      formatSpec.c_str());
    return false;
}

bool ELogFormatter::getFieldCloseParen(const std::string& formatSpec, std::string::size_type from,
                                       std::string::size_type& closePos) {
    // it is expected to have the first char as open brace
    int count = 0;
    bool countChanged = false;
    while (from < formatSpec.length()) {
        if (formatSpec[from] == '(') {
            ++count;
            countChanged = true;
        } else if (formatSpec[from] == ')') {
            --count;
            countChanged = true;
        }
        if (count < 0) {
            ELOG_REPORT_ERROR(
                "Invalid predicate specification, nested expression syntax error at position %zu: "
                "%s",
                from, formatSpec.c_str());
            return false;
        }
        if (count == 0 && countChanged) {
            closePos = from;
            return true;
        }
        ++from;
    }

    ELOG_REPORT_ERROR(
        "Invalid predicate specification, nested expression missing close parenthesis: %s",
        formatSpec.c_str());
    return false;
}

bool ELogFormatter::parseSimpleField(const std::string& fieldSpecStr) {
    // parse the field
    ELogFieldSpec fieldSpec;
    if (!fieldSpec.parse(fieldSpecStr)) {
        return false;
    }

    // let sub-classes handle event (this adds also a selector)
    return handleField(fieldSpec);
}

bool ELogFormatter::parseCondField(const std::string& fieldSpecStr) {
    // expected format (stripped from enclosing ${}):
    // ${if: (filter-pred): ${name:<true format>} [: ${name:< false format>}] }
    std::string::size_type colonPos = fieldSpecStr.find(':');
    std::string name = fieldSpecStr.substr(0, colonPos);
    if (name.compare("if") != 0) {
        ELOG_REPORT_ERROR("Internal error, expecting 'if' keyword for conditional formatting: %s",
                          fieldSpecStr.c_str());
        return false;
    }

    // get filter part, but be careful, since filter might contain colon, so we need to rely on
    // parenthesis for correct parsing
    std::string suffix = trim(fieldSpecStr.substr(colonPos + 1));
    if (!suffix.starts_with("(")) {
        ELOG_REPORT_ERROR(
            "Invalid filter in conditional formatting specification, filter must be enclosed with "
            "'()' - missing starting parenthesis: %s",
            fieldSpecStr.c_str());
        return false;
    }
    std::string::size_type closeParenPos = std::string::npos;
    if (!getFieldCloseParen(suffix, 0, closeParenPos)) {
        ELOG_REPORT_ERROR(
            "Invalid filter in conditional formatting specification, filter must be enclosed with "
            "'()' - missing closing parenthesis: %s",
            fieldSpecStr.c_str());
        return false;
    }
    std::string filterStr = suffix.substr(0, closeParenPos + 1);
    ELogFilter* filter = ELogConfigLoader::loadLogFilterExprStr(filterStr.c_str());
    if (filter == nullptr) {
        ELOG_REPORT_ERROR(
            "Invalid filter expression '%s' in conditional formatting specification: %s",
            filterStr.c_str(), fieldSpecStr.c_str());
        return false;
    }

    // filter should be followed by colon
    colonPos = suffix.find(':', closeParenPos + 1);
    if (colonPos == std::string::npos) {
        ELOG_REPORT_ERROR(
            "Invalid conditional formatting specification, expecting colon ':' after filter "
            "expression: %s",
            fieldSpecStr.c_str());
        return false;
    }

    // be careful now, since format clause contains colon, so we need to get the boundary of
    // ${...} expression instead
    std::string trueSpecStr = trim(suffix.substr(colonPos + 1));
    ELogFieldSelector* trueSelector = loadSelector(trueSpecStr);
    if (trueSelector == nullptr) {
        ELOG_REPORT_ERROR(
            "Invalid true-clause '%s' in conditional formatting specification (field parsing "
            "failed): %s",
            trueSpecStr.c_str(), fieldSpecStr.c_str());
        delete filter;
        return false;
    }

    // finally we check for optional false clause, it should be after end of true clause, separated
    // by colon
    ELogFieldSelector* falseSelector = nullptr;
    std::string::size_type endPos = suffix.find("}");
    colonPos = suffix.find(':', endPos);
    if (colonPos != std::string::npos) {
        suffix = trim(suffix.substr(colonPos + 1));
        falseSelector = loadSelector(suffix);
        if (falseSelector == nullptr) {
            ELOG_REPORT_ERROR(
                "Invalid false-clause '%s' in conditional formatting specification (field parsing "
                "failed): %s",
                suffix.c_str(), fieldSpecStr.c_str());
            delete trueSelector;
            delete filter;
            return false;
        }

        endPos = suffix.find("}");
    }

    // there should be no excess chars
    suffix = trim(suffix.substr(endPos + 1));
    if (!suffix.empty()) {
        ELOG_REPORT_ERROR("Excess character '%s' in conditional formatting specification: %s",
                          suffix.c_str(), fieldSpecStr.c_str());
        if (falseSelector != nullptr) {
            delete falseSelector;
        }
        delete trueSelector;
        delete filter;
        return false;
    }

    // finally we can build the conditional selector
    ELogIfSelector* fieldSelector =
        new (std::nothrow) ELogIfSelector(filter, trueSelector, falseSelector);
    if (fieldSelector == nullptr) {
        ELOG_REPORT_ERROR("Failed to allocate conditional field selector, out of memory");
        if (falseSelector != nullptr) {
            delete falseSelector;
        }
        delete trueSelector;
        delete filter;
        return false;
    }

    // NOTE: we do not call handleField for conditional format selector
    m_fieldSelectors.push_back(fieldSelector);
    return true;
}

bool ELogFormatter::parseSwitchField(const std::string& fieldSpecStr) {
    // expected format (stripped from enclosing ${}):
    // ${switch: (expr): ${case: (expr) : ${name:<format>}}, ..., ${default:${name: <format>}} }
    // the expr is ELogFieldSelector that yields int, string, level or time.
    // for instance:
    // ${switch: ${level}:
    //      ${case: ${const-level:ERROR} : ${fmt:fg-color=red}} :
    //      ${case: ${const-level:WARN}  : ${fmt:fg-color=yellow}} :
    //      ${case: ${const-level:TRACE} : ${fmt:text=faint}} :
    //      ${default                    : ${fmt:fg-color=green}}
    // }
    // text can be spanned in multiple lines, which will not appear in final formatted log line
    std::string::size_type colonPos = fieldSpecStr.find(':');
    std::string name = fieldSpecStr.substr(0, colonPos);
    if (name.compare("switch") != 0) {
        ELOG_REPORT_ERROR(
            "Internal error, expecting 'switch' keyword for conditional formatting: %s",
            fieldSpecStr.c_str());
        return false;
    }

    // get expression part
    std::string::size_type nextColonPos = fieldSpecStr.find(':', colonPos + 1);
    if (nextColonPos == std::string::npos) {
        ELOG_REPORT_ERROR(
            "Invalid conditional formatting specification, expecting colon ':' after switch "
            "expression: %s",
            fieldSpecStr.c_str());
        return false;
    }

    // parse expression (which cannot contain any colon char)
    std::string exprStr = trim(fieldSpecStr.substr(colonPos + 1, nextColonPos - colonPos - 1));
    ELogFieldSelector* exprSelector = loadSelector(exprStr.c_str());
    if (exprSelector == nullptr) {
        ELOG_REPORT_ERROR(
            "Invalid switch expression '%s' in conditional formatting specification: %s",
            exprStr.c_str(), fieldSpecStr.c_str());
        return false;
    }

    ELogSwitchSelector* switchSelector = new (std::nothrow) ELogSwitchSelector(exprSelector);
    if (switchSelector == nullptr) {
        ELOG_REPORT_ERROR("Failed to allocate switch field selector, out of memory");
        delete exprSelector;
        return false;
    }

    // now parse cases
    bool done = false;
    while (!done) {
        // move past colon, and we expect to see at least one case clause
        if (!trim(fieldSpecStr.substr(nextColonPos + 1)).starts_with("${")) {
            ELOG_REPORT_ERROR(
                "Case expression expected after value expression in conditional formatting "
                "specification: %s",
                fieldSpecStr.c_str());
            delete switchSelector;
            return false;
        }
        std::string::size_type openBracePos = fieldSpecStr.find("${", nextColonPos + 1);
        std::string::size_type closeBracePos = std::string::npos;
        if (!getFieldCloseBrace(fieldSpecStr, openBracePos, closeBracePos)) {
            ELOG_REPORT_ERROR(
                "Invalid case expression syntax in conditional formatting specification: %s",
                fieldSpecStr.c_str());
            delete switchSelector;
            return false;
        }

        bool isDefaultClause = false;
        if (!parseCaseOrDefaultClause(
                switchSelector,
                fieldSpecStr.substr(openBracePos + 2, closeBracePos - openBracePos - 2),
                isDefaultClause)) {
            ELOG_REPORT_ERROR(
                "Failed to parse case clause in conditional formatting specification: %s",
                fieldSpecStr.c_str());
            delete switchSelector;
            return false;
        }

        if (isDefaultClause) {
            // make sure that all characters left are white space
            std::string suffix = fieldSpecStr.substr(closeBracePos + 1);
            if (!trim(suffix).empty()) {
                ELOG_REPORT_ERROR(
                    "Excess characters '%s' found in conditional formatting specification: %s",
                    fieldSpecStr.c_str(), suffix.c_str());
                delete switchSelector;
                return false;
            }
            done = true;
        } else {
            // search for colon after case expression
            nextColonPos = fieldSpecStr.find(':', closeBracePos + 1);
            if (nextColonPos == std::string::npos) {
                // it is possible not to have default clause, in which case all characters left must
                // be white space
                std::string suffix = fieldSpecStr.substr(closeBracePos + 1);
                if (!trim(suffix).empty()) {
                    ELOG_REPORT_ERROR(
                        "Excess characters '%s' found in conditional formatting specification: %s",
                        fieldSpecStr.c_str(), suffix.c_str());
                    delete switchSelector;
                    return false;
                }
                done = true;
            }
        }
    }

    // NOTE: we do not call handleField for conditional format selector
    m_fieldSelectors.push_back(switchSelector);
    return true;
}

// TODO: reset format automatically at the end of formatted log line in case at least one format
// escape code was used

bool ELogFormatter::parseExprSwitchField(const std::string& fieldSpecStr) {
    // expected format:
    // ${expr-switch: ${case: (pred) : ${name:<format>}}, ..., ${default:${name: <format>}} }
    // the pred is ELogFilter.
    // for instance:
    // ${expr-switch:
    //      ${case: (src == core.files) : ${fmt:fg-color=red}}
    //      ${case: (level == WARN)     : ${fmt:fg-color=yellow}}
    //      ${default                   : ${fmt:fg-color=green}}
    // }
    // text can be spanned in multiple lines, which will not appear in final formatted log line
    std::string::size_type colonPos = fieldSpecStr.find(':');
    std::string name = fieldSpecStr.substr(0, colonPos);
    if (name.compare("expr-switch") != 0) {
        ELOG_REPORT_ERROR(
            "Internal error, expecting 'expr-switch' keyword for conditional formatting: %s",
            fieldSpecStr.c_str());
        return false;
    }

    // make sure no excess chars are found between expr-switch and ':'
    size_t prefixLen = strlen("expr-switch");
    if (!trim(fieldSpecStr.substr(prefixLen, colonPos - prefixLen)).empty()) {
        ELOG_REPORT_ERROR("Excess characters found after 'expr-switch': %s", fieldSpecStr.c_str());
        return false;
    }

    ELogExprSwitchSelector* switchSelector = new (std::nothrow) ELogExprSwitchSelector();
    if (switchSelector == nullptr) {
        ELOG_REPORT_ERROR("Failed to allocate switch field selector, out of memory");
        return false;
    }

    // now parse cases
    bool done = false;
    while (!done) {
        // move past colon, and we expect to see at least one case clause
        if (!trim(fieldSpecStr.substr(colonPos + 1)).starts_with("${")) {
            ELOG_REPORT_ERROR(
                "Case expression expected after value expression in expr-switch formatting "
                "specification: %s",
                fieldSpecStr.c_str());
            delete switchSelector;
            return false;
        }
        std::string::size_type openBracePos = fieldSpecStr.find("${", colonPos + 1);
        std::string::size_type closeBracePos = std::string::npos;
        if (!getFieldCloseBrace(fieldSpecStr, openBracePos, closeBracePos)) {
            ELOG_REPORT_ERROR(
                "Invalid case expression syntax in expr-switch formatting specification: %s",
                fieldSpecStr.c_str());
            delete switchSelector;
            return false;
        }

        bool isDefaultClause = false;
        if (!parseExprCaseOrDefaultClause(
                switchSelector,
                fieldSpecStr.substr(openBracePos + 2, closeBracePos - openBracePos - 2),
                isDefaultClause)) {
            ELOG_REPORT_ERROR(
                "Failed to parse case clause in expr-switch formatting specification: %s",
                fieldSpecStr.c_str());
            delete switchSelector;
            return false;
        }

        if (isDefaultClause) {
            // make sure that all characters left are white space
            std::string suffix = fieldSpecStr.substr(closeBracePos + 1);
            if (!trim(suffix).empty()) {
                ELOG_REPORT_ERROR(
                    "Excess characters '%s' found in expr-switch formatting specification: %s",
                    fieldSpecStr.c_str(), suffix.c_str());
                delete switchSelector;
                return false;
            }
            done = true;
        } else {
            // search for colon after case expression
            colonPos = fieldSpecStr.find(':', closeBracePos + 1);
            if (colonPos == std::string::npos) {
                // it is possible not to have default clause, in which case all characters left must
                // be white space
                std::string suffix = fieldSpecStr.substr(closeBracePos + 1);
                if (!trim(suffix).empty()) {
                    ELOG_REPORT_ERROR(
                        "Excess characters '%s' found in expr-switch formatting specification: %s",
                        fieldSpecStr.c_str(), suffix.c_str());
                    delete switchSelector;
                    return false;
                }
                done = true;
            }
        }
    }

    // NOTE: we do not call handleField for conditional format selector
    m_fieldSelectors.push_back(switchSelector);
    return true;
}

bool ELogFormatter::parseCaseOrDefaultClause(ELogSwitchSelector* switchSelector,
                                             const std::string& caseSpec, bool& isDefaultClause) {
    // expected format is (enclosing ${} stripped):
    // ${case: ${const-level:TRACE} : ${fmt:text=faint}}
    // or:
    // ${default                    : ${fmt:fg-color=green}}
    std::string::size_type colonPos = caseSpec.find(':');
    if (colonPos == std::string::npos) {
        ELOG_REPORT_ERROR(
            "Invalid case-value specification, expected case/default followed by colon: %s",
            caseSpec.c_str());
        return false;
    }
    std::string name = trim(caseSpec.substr(0, colonPos));
    if (name.compare("case") == 0) {
        // parse case clause
        isDefaultClause = false;
        return parseCaseClause(switchSelector, trim(caseSpec.substr(colonPos + 1)));
    } else if (name.compare("default") == 0) {
        // parse default clause
        isDefaultClause = true;
        return parseDefaultClause(switchSelector, trim(caseSpec.substr(colonPos + 1)));
    } else {
        ELOG_REPORT_ERROR("Invalid switch expression, expected 'case' or 'default': %s",
                          caseSpec.c_str());
        return false;
    }
}

bool ELogFormatter::parseCaseClause(ELogSwitchSelector* switchSelector,
                                    const std::string& caseSpec) {
    // expected format is (enclosing ${} stripped):
    // ${const-level:TRACE} : ${fmt:text=faint}
    if (!caseSpec.starts_with("${")) {
        ELOG_REPORT_ERROR(
            "Invalid case value syntax in switch formatting specification, should start with '${': "
            "%s",
            caseSpec.c_str());
        return false;
    }

    std::string::size_type closeBracePos = std::string::npos;
    if (!getFieldCloseBrace(caseSpec, 0, closeBracePos)) {
        ELOG_REPORT_ERROR("Invalid case value syntax in switch formatting specification: %s",
                          caseSpec.c_str());
        delete switchSelector;
        return false;
    }

    std::string valueSpec = trim(caseSpec.substr(0, closeBracePos + 1));
    ELogFieldSelector* valueSelector = loadSelector(valueSpec);
    if (valueSelector == nullptr) {
        ELOG_REPORT_ERROR(
            "Failed to load value selector for switch format specification, invalid expression: %s",
            valueSpec.c_str());
        delete switchSelector;
        return false;
    }

    // parse expected colon
    std::string::size_type colonPos = caseSpec.find(':', closeBracePos + 1);
    if (colonPos == std::string::npos) {
        ELOG_REPORT_ERROR(
            "Invalid switch formatting specification, missing ':' after case value: %s",
            caseSpec.c_str());
        delete switchSelector;
        return false;
    }

    std::string resultSpec = trim(caseSpec.substr(colonPos + 1));
    ELogFieldSelector* resultSelector = loadSelector(resultSpec);
    if (resultSelector == nullptr) {
        ELOG_REPORT_ERROR(
            "Failed to load result selector for switch format specification, invalid expression: "
            "%s",
            resultSpec.c_str());
        delete valueSelector;
        delete switchSelector;
        return false;
    }

    switchSelector->addCase(valueSelector, resultSelector);
    return true;
}

bool ELogFormatter::parseDefaultClause(ELogSwitchSelector* switchSelector,
                                       const std::string& defaultSpec) {
    // expected format is (enclosing ${} stripped):
    // ${fmt:fg-color=green}
    ELogFieldSelector* defaultSelector = loadSelector(defaultSpec);
    if (defaultSelector == nullptr) {
        ELOG_REPORT_ERROR("Invalid default-clause specification: %s", defaultSpec.c_str());
        return false;
    }
    switchSelector->addDefaultCase(defaultSelector);
    return true;
}

bool ELogFormatter::parseExprCaseOrDefaultClause(ELogExprSwitchSelector* switchSelector,
                                                 const std::string& caseSpec,
                                                 bool& isDefaultClause) {
    // expected format is (enclosing ${} stripped):
    //      ${case: (${src} == ${const-string: "core.files"}) : ${fmt:fg-color=red}}
    // or:
    //      ${default                    : ${fmt:fg-color=green}}
    std::string::size_type colonPos = caseSpec.find(':');
    if (colonPos == std::string::npos) {
        ELOG_REPORT_ERROR(
            "Invalid case-value specification, expected case/default followed by colon: %s",
            caseSpec.c_str());
        return false;
    }
    std::string name = trim(caseSpec.substr(0, colonPos));
    if (name.compare("case") == 0) {
        // parse case clause
        isDefaultClause = false;
        return parseExprCaseClause(switchSelector, trim(caseSpec.substr(colonPos + 1)));
    } else if (name.compare("default") == 0) {
        // parse default clause
        isDefaultClause = true;
        return parseExprDefaultClause(switchSelector, trim(caseSpec.substr(colonPos + 1)));
    } else {
        ELOG_REPORT_ERROR("Invalid switch expression, expected 'case' or 'default': %s",
                          caseSpec.c_str());
        return false;
    }
}

bool ELogFormatter::parseExprCaseClause(ELogExprSwitchSelector* switchSelector,
                                        const std::string& caseSpec) {
    // expected format is (enclosing ${} stripped):
    //      ${case: (${src} == ${const-string: "core.files"}) : ${fmt:fg-color=red}}
    if (!caseSpec.starts_with("(")) {
        ELOG_REPORT_ERROR(
            "Invalid case predicate syntax in expr-switch formatting specification, should start "
            "with '(': %s",
            caseSpec.c_str());
        return false;
    }

    std::string::size_type closeParenPos = std::string::npos;
    if (!getFieldCloseParen(caseSpec, 0, closeParenPos)) {
        ELOG_REPORT_ERROR(
            "Invalid case predicate syntax in expr-switch formatting specification: %s",
            caseSpec.c_str());
        delete switchSelector;
        return false;
    }

    std::string predSpec = trim(caseSpec.substr(0, closeParenPos + 1));
    ELogFilter* valueFilter = ELogConfigLoader::loadLogFilterExprStr(predSpec.c_str());
    if (valueFilter == nullptr) {
        ELOG_REPORT_ERROR(
            "Failed to load value selector for expr-switch format specification, invalid "
            "predicate: %s",
            predSpec.c_str());
        delete switchSelector;
        return false;
    }

    // parse expected colon
    std::string::size_type colonPos = caseSpec.find(':', closeParenPos + 1);
    if (colonPos == std::string::npos) {
        ELOG_REPORT_ERROR(
            "Invalid expr-switch formatting specification, missing ':' after case predicate: %s",
            caseSpec.c_str());
        delete switchSelector;
        return false;
    }

    std::string resultSpec = trim(caseSpec.substr(colonPos + 1));
    ELogFieldSelector* resultSelector = loadSelector(resultSpec);
    if (resultSelector == nullptr) {
        ELOG_REPORT_ERROR(
            "Failed to load result selector for switch format specification, invalid expression: "
            "%s",
            resultSpec.c_str());
        delete valueFilter;
        delete switchSelector;
        return false;
    }

    switchSelector->addCase(valueFilter, resultSelector);
    return true;
}

bool ELogFormatter::parseExprDefaultClause(ELogExprSwitchSelector* switchSelector,
                                           const std::string& defaultSpec) {
    // expected format is (enclosing ${} stripped):
    // ${fmt:fg-color=green}
    ELogFieldSelector* defaultSelector = loadSelector(defaultSpec);
    if (defaultSelector == nullptr) {
        ELOG_REPORT_ERROR("Invalid default-clause specification: %s", defaultSpec.c_str());
        return false;
    }
    switchSelector->addDefaultCase(defaultSelector);
    return true;
}

ELogFieldSelector* ELogFormatter::loadSelector(const std::string& selectorSpecStr) {
    if (!selectorSpecStr.starts_with("${")) {
        ELOG_REPORT_ERROR("Invalid field selector specification, missing initial '${': %s",
                          selectorSpecStr.c_str());
        return nullptr;
    }
    std::string::size_type endPos = selectorSpecStr.find("}");
    if (endPos == std::string::npos) {
        ELOG_REPORT_ERROR("Invalid field selector specification, missing enclosing '}': %s",
                          selectorSpecStr.c_str());
        return nullptr;
    }

    // now parse simple field and create selector
    // NOTE: we do not restrict here to fmt field selector, in fact we allow here to specify
    // conditional log line format, that is different fields can be issued under different
    // conditions, which is even a more generic functionality
    std::string fieldSpecStr = selectorSpecStr.substr(2, endPos - 2);

    // handle first special case: const fields
    std::string::size_type colonPos = fieldSpecStr.find(':');
    if (colonPos != std::string::npos) {
        std::string name = trim(fieldSpecStr.substr(0, colonPos));
        if (name.starts_with("const-")) {
            return loadConstSelector(fieldSpecStr);
        }
    }

    ELogFieldSpec fieldSpec;
    if (!fieldSpec.parse(fieldSpecStr)) {
        ELOG_REPORT_ERROR("Failed parsing field selector: %s", fieldSpecStr.c_str());
        return nullptr;
    }

    // create the field selector
    ELogFieldSelector* selector = constructFieldSelector(fieldSpec);
    if (selector == nullptr) {
        ELOG_REPORT_ERROR("Failed to create field selector from specification: %s",
                          fieldSpecStr.c_str());
    }
    return selector;
}

ELogFieldSelector* ELogFormatter::loadConstSelector(const std::string& fieldSpecStr) {
    std::string::size_type colonPos = fieldSpecStr.find(':');
    if (colonPos == std::string::npos) {
        ELOG_REPORT_ERROR("Invalid const field selector specification, missing ':' separator: %s",
                          fieldSpecStr.c_str());
        return nullptr;
    }
    std::string name = trim(fieldSpecStr.substr(0, colonPos));
    std::string value = trim(fieldSpecStr.substr(colonPos + 1));

    ELogFieldSelector* selector = nullptr;
    if (name.compare("const-int") == 0) {
        uint64_t intValue = 0;
        if (!parseIntProp("const-int", "", value, intValue)) {
            ELOG_REPORT_ERROR("Invalid integer value specified for const-int field selector: %s",
                              value.c_str());
        }
        selector = new (std::nothrow) ELogConstIntSelector(intValue);
    } else if (name.compare("const-string") == 0) {
        // we may have surrounding quotes
        if (value.starts_with("\"")) {
            if (!value.ends_with("\"")) {
                ELOG_REPORT_ERROR(
                    "Invalid string value specified for const-string field selector, missing end "
                    "quote: %s",
                    value.c_str());
                return nullptr;
            } else {
                // no trimming, as it is quoted
                value = value.substr(1, value.length() - 2);
            }
        } else if (value.ends_with("\"")) {
            ELOG_REPORT_ERROR(
                "Invalid string value specified for const-string field selector, missing start "
                "quote: %s",
                value.c_str());
            return nullptr;
        }
        selector = new (std::nothrow) ELogConstStringSelector(value.c_str());
    } else if (name.compare("const-time") == 0) {
        ELogTime logTime;
        if (!elogTimeFromString(value.c_str(), logTime)) {
            ELOG_REPORT_ERROR("Invalid time value specified for const-time field selector: %s",
                              value.c_str());
            return nullptr;
        }
        selector = new (std::nothrow) ELogConstTimeSelector(logTime, value.c_str());
    } else if (name.compare("const-level") == 0) {
        ELogLevel logLevel;
        if (!elogLevelFromStr(value.c_str(), logLevel)) {
            ELOG_REPORT_ERROR(
                "Invalid log level value specified for const-level field selector: %s",
                value.c_str());
            return nullptr;
        }
        selector = new (std::nothrow) ELogConstLogLevelSelector(logLevel);
    }

    if (selector == nullptr) {
        ELOG_REPORT_ERROR("Failed to create const-selector, out of memory");
    }
    return selector;
}

}  // namespace elog
