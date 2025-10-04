#include "elog_config.h"

#include <cassert>
#include <cinttypes>
#include <sstream>

#include "elog_common.h"
#include "elog_config_loader.h"
#include "elog_report.h"
#include "elog_string_tokenizer.h"

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogConfig)

static const char* sNodeTypeStr[] = {"simple", "array", "map"};
static const size_t sNodeTypeCount = sizeof(sNodeTypeStr) / sizeof(sNodeTypeStr[0]);

static const char* sValueTypeStr[] = {"null", "boolean", "integer", "string", "array", "map"};
static const size_t sValueTypeCount = sizeof(sValueTypeStr) / sizeof(sValueTypeStr[0]);

ELogConfigMapNode::EntryType ELogConfigMapNode::sNullEntry = {"", nullptr};

/** @brief Key-value separator configuration mode constants. */
enum class ELogConfigMode : uint32_t {
    /** @brief Use equals sign to separate key and value. */
    CM_EQUALS,

    /** @brief Use colon sign to separate key and value. */
    CM_COLON,

    /**
     * @brief Use any sign, either equals or colon, to separate key and value. Each time a different
     * sign can be used.
     */
    CM_ANY,

    /**
     * @brief Use any sign, either equals or colon, to separate key and value, but be consistent
     * (i.e. either always equals sign or always use colon sign but not both intermixed).
     */
    CM_CONSISTENT
};

static ELogConfigNode* parseConfigNode(ELogStringTokenizer& tok,
                                       ELogConfigSourceContext* sourceContext,
                                       ELogConfigMode configMode = ELogConfigMode::CM_CONSISTENT);

const char* configNodeTypeToString(ELogConfigNodeType nodeType) {
    if (((size_t)nodeType) < sNodeTypeCount) {
        return sNodeTypeStr[(size_t)nodeType];
    }
    return "N/A";
}

const char* configValueTypeToString(ELogConfigValueType valueType) {
    if (((size_t)valueType) < sValueTypeCount) {
        return sValueTypeStr[(size_t)valueType];
    }
    return "N/A";
}

std::string ELogConfigSourceContext::getPosContext(size_t pos, const char* pathContext) const {
    // find source line
    uint32_t lineDataIndex = 0;
    uint32_t lineNumber = 0;
    size_t offset = 0;
    size_t totalChars = 0;
    for (const auto& lineData : m_lines) {
        if (pos >= totalChars && pos < totalChars + lineData.second.length()) {
            // line found
            lineNumber = lineData.first;
            offset = pos - totalChars;
            break;
        }
        totalChars += lineData.second.length();
        ++lineDataIndex;
    }
    if (lineNumber == 0) {
        return "";
    }

    const std::pair<uint32_t, std::string>& lineData = m_lines[lineDataIndex];
    std::stringstream s;
    s << "line: " << lineData.first << ", offset: " << offset << ", node path: " << pathContext
      << ", source: " << lineData.second.substr(0, offset) << RED " | HERE ===>>> | " RESET
      << lineData.second.substr(offset);
    return s.str();
}

const char* ELogConfigContext::getFullContext() const {
    if (m_parsePos == ELOG_CONFIG_INVALID_PARSE_POS) {
        return m_pathContext.c_str();
    }
    if (m_fullContext.empty()) {
        m_fullContext = m_sourceContext->getPosContext(m_parsePos, m_pathContext.c_str());
    }
    return m_fullContext.c_str();
}

ELogConfigContext* ELogConfigEntity::makeConfigContext(
    size_t parsePos /* = ELOG_CONFIG_INVALID_PARSE_POS */) {
    ELogConfigContext* context =
        new (std::nothrow) ELogConfigContext(getSourceContext(), parsePos, "");
    if (context == nullptr) {
        ELOG_REPORT_ERROR("Failed to allocate configuration context, out of memory (context : %s)",
                          getFullContext());
    }
    return context;
}

void ELogConfigArrayNode::addValue(ELogConfigValue* value) {
    setValuePathContext(value, m_values.size());
    m_values.push_back(value);
}

void ELogConfigArrayNode::onSetPathContext(const char* pathContext) {
    for (size_t i = 0; i < m_values.size(); ++i) {
        ELogConfigValue* value = m_values[i];
        setValuePathContext(value, i);
    }
}

void ELogConfigArrayNode::setValuePathContext(ELogConfigValue* value, size_t index) {
    std::stringstream s;
    s << getPathContext() << "::array[" << index << "]";
    value->setPathContext(s.str().c_str());
}

bool ELogConfigMapNode::addEntry(const char* key, ELogConfigValue* value) {
    bool res = m_entryMap.insert(EntryMap::value_type(key, m_entries.size())).second;
    if (res) {
        setValuePathContext(key, value);
        m_entries.push_back({key, value});
    }
    return res;
}

bool ELogConfigMapNode::mergeStringEntry(const char* key, const char* value) {
    // try to insert a new entry
    std::pair<EntryMap::iterator, bool> itrRes =
        m_entryMap.insert(EntryMap::value_type(key, m_entries.size()));

    if (itrRes.second) {
        // insertion succeeded (key does not exist), so now create a stirng value and add it
        ELogConfigContext* context = makeConfigContext();
        if (context == nullptr) {
            ELOG_REPORT_ERROR("Failed to merge entry %s/%s to map node (context : %s)", key, value,
                              getFullContext());
            return false;
        }
        ELogConfigStringValue* strValue = new (std::nothrow) ELogConfigStringValue(context, value);
        if (strValue == nullptr) {
            ELOG_REPORT_ERROR(
                "Failed to allocate string configuration value for key %s, out of memory (context: "
                "%s)",
                key, getFullContext());
            m_entryMap.erase(itrRes.first);
            delete context;
            return false;
        }
        setValuePathContext(key, strValue);
        m_entries.push_back({key, strValue});
        return true;
    } else {
        // otherwise, retrieve the mapped value, but check first it is of string type
        ELogConfigValue* cfgValue = m_entries[itrRes.first->second].second;
        if (cfgValue->getValueType() != ELogConfigValueType::ELOG_CONFIG_STRING_VALUE) {
            ELOG_REPORT_ERROR(
                "Cannot merge string configuration value for key %s, existing value is not of "
                "string type (context: %s)",
                key, cfgValue->getFullContext());
            return false;
        }
        ((ELogConfigStringValue*)cfgValue)->setStringValue(value);
        return true;
    }
}

bool ELogConfigMapNode::mergeIntEntry(const char* key, int64_t value) {
    // try to insert a new entry
    std::pair<EntryMap::iterator, bool> itrRes =
        m_entryMap.insert(EntryMap::value_type(key, m_entries.size()));

    if (itrRes.second) {
        // insertion succeeded (key does not exist), so now create a integer value and add it
        ELogConfigContext* context = makeConfigContext();
        if (context == nullptr) {
            ELOG_REPORT_ERROR("Failed to merge entry %s/%" PRId64 " to map node (context : %s)",
                              key, value, getFullContext());
            return false;
        }
        ELogConfigIntValue* intValue = new (std::nothrow) ELogConfigIntValue(context, value);
        if (intValue == nullptr) {
            ELOG_REPORT_ERROR(
                "Failed to allocate integer configuration value for key %s, out of memory", key);
            m_entryMap.erase(itrRes.first);
            delete context;
            return false;
        }
        setValuePathContext(key, intValue);
        m_entries.push_back({key, intValue});
        return true;
    } else {
        // otherwise, retrieve the mapped value, but check first it is of integer type
        ELogConfigValue* cfgValue = m_entries[itrRes.first->second].second;
        if (cfgValue->getValueType() != ELogConfigValueType::ELOG_CONFIG_INT_VALUE) {
            ELOG_REPORT_ERROR(
                "Cannot merge integer configuration value for key %s, existing value is not of "
                "integer type (context: %s)",
                key, cfgValue->getFullContext());
            return false;
        }
        ((ELogConfigIntValue*)cfgValue)->setIntValue(value);
        return true;
    }
}

bool ELogConfigMapNode::getStringValue(const char* key, bool& found, std::string& value) const {
    const ELogConfigValue* cfgValue = getValue(key);
    if (cfgValue == nullptr) {
        found = false;
        return true;
    }
    found = true;
    if (cfgValue->getValueType() != ELogConfigValueType::ELOG_CONFIG_STRING_VALUE) {
        ELOG_REPORT_ERROR(
            "Invalid configuration value type for %s, expecting string, seeing instead type %s "
            "(context: %s)",
            key, configValueTypeToString(cfgValue->getValueType()), cfgValue->getFullContext());
        return false;
    }
    value = ((const ELogConfigStringValue*)cfgValue)->getStringValue();
    return true;
}

bool ELogConfigMapNode::getIntValue(const char* key, bool& found, int64_t& value) const {
    const ELogConfigValue* cfgValue = getValue(key);
    if (cfgValue == nullptr) {
        found = false;
        return true;
    }
    found = true;
    if (cfgValue->getValueType() != ELogConfigValueType::ELOG_CONFIG_INT_VALUE) {
        ELOG_REPORT_ERROR(
            "Invalid configuration value type for %s, expecting integer, seeing instead type %s "
            "(context: %s)",
            key, configValueTypeToString(cfgValue->getValueType()), cfgValue->getFullContext());
        return false;
    }
    value = ((const ELogConfigIntValue*)cfgValue)->getIntValue();
    return true;
}

bool ELogConfigMapNode::getBoolValue(const char* key, bool& found, bool& value) const {
    const ELogConfigValue* cfgValue = getValue(key);
    if (cfgValue == nullptr) {
        found = false;
        return true;
    }
    found = true;
    if (cfgValue->getValueType() != ELogConfigValueType::ELOG_CONFIG_BOOL_VALUE) {
        ELOG_REPORT_ERROR(
            "Invalid configuration value type for %s, expecting boolean, seeing instead type %s "
            "(context: %s)",
            key, configValueTypeToString(cfgValue->getValueType()), cfgValue->getFullContext());
        return false;
    }
    value = ((const ELogConfigBoolValue*)cfgValue)->getBoolValue();
    return true;
}

void ELogConfigMapNode::onSetPathContext(const char* pathContext) {
    for (const auto& entry : m_entryMap) {
        const char* key = entry.first.c_str();
        size_t index = entry.second;
        ELogConfigValue* value = m_entries[index].second;
        setValuePathContext(key, value);
    }
}

void ELogConfigMapNode::setValuePathContext(const char* key, ELogConfigValue* value) {
    std::stringstream s;
    s << getPathContext() << "::map[" << key << "]";
    value->setPathContext(s.str().c_str());
}

ELogConfig* ELogConfig::loadFromFile(const char* path) {
    // read all lines, combine into a single string and load
    // finally add context info (line/column source text)
    std::vector<std::pair<uint32_t, std::string>> lines;
    if (!ELogConfigLoader::loadFile(path, lines)) {
        ELOG_REPORT_ERROR("Failed to load configuration from file path %s", path);
        return nullptr;
    }

    ELogConfigSourceContext* sourceContext = new (std::nothrow) ELogConfigSourceContext(path);
    if (sourceContext == nullptr) {
        ELOG_REPORT_ERROR("Failed to allocate configuration source context, out of memory");
        return nullptr;
    }

    // convert all lines to a single string
    std::string cfgString;
    for (const auto& lineData : lines) {
        cfgString += lineData.second;
        sourceContext->addLineData(lineData.first, lineData.second.c_str());
    }

    ELogConfig* cfg = load(cfgString.c_str(), sourceContext);
    if (cfg == nullptr) {
        delete sourceContext;
        return nullptr;
    }

    // set context data and return
    cfg->m_sourceContext = sourceContext;
    return cfg;
}

ELogConfig* ELogConfig::loadFromPropFile(const char* path) {
    // we need to load properties from file lines with position information
    // so we first load line information using helper function (at ELogConfigLoader).
    // then we parse each line, as "key = value" with additional absolute position information.
    // for this to work correctly we must disregard empty lines and full comment lines (since the
    // source context object works on valid lines only). This is already handled by the helper
    // function. finally, multiline properties should have special treatment in order to be mapped
    // correctly to source location. Normally it would have been flattened to a single string,
    // adding spaces between lines, but this method of work would lose position information, both
    // line numbers, and absolute position, which will drift away by one char for each line. one way
    // to solve this, is to have the source context support complex lines, that is line data becomes
    // a pair of base line number, and vector of lines. this way, when the flattened string is used
    // as property value, any position within the flattened string, can be correctly mapped to
    // source location. still this requires not adding extra spaces.

    // current we use a simpler solution, that is tweaking a bit the input file to be a valid
    // configuration file

    // read all lines, combine into a single string and load
    std::vector<std::pair<uint32_t, std::string>> lines;
    if (!ELogConfigLoader::loadFile(path, lines)) {
        ELOG_REPORT_ERROR("Failed to load configuration from file path %s", path);
        return nullptr;
    }

    // add context info
    ELogConfigSourceContext* sourceContext = new (std::nothrow) ELogConfigSourceContext(path);
    if (sourceContext == nullptr) {
        ELOG_REPORT_ERROR("Failed to allocate configuration source context, out of memory");
        return nullptr;
    }

    // convert all lines to a single string, while adding context info
    // we fool the load function, by transforming the input string into a valid configuration
    // string, by adding comma at each end of line (except for last one), and wrapping the whole
    // text with braces, so it becomes a one large map node
    std::string cfgString = "{";
    for (uint32_t i = 0; i < lines.size(); ++i) {
        const auto& lineData = lines[i];
        cfgString += lineData.second;
        if (i + 1 < lines.size()) {
            cfgString += ",";
        }
        sourceContext->addLineData(lineData.first, lineData.second.c_str());
    }
    cfgString += "}";

    ELogConfig* cfg = load(cfgString.c_str(), sourceContext);
    if (cfg == nullptr) {
        delete sourceContext;
        return nullptr;
    }

    // set context data and return
    cfg->m_sourceContext = sourceContext;
    return cfg;
}

ELogConfig* ELogConfig::loadFromString(const char* str) {
    // prepare artificial context data and parse
    ELogConfigSourceContext* sourceContext = new (std::nothrow) ELogConfigSourceContext();
    if (sourceContext == nullptr) {
        ELOG_REPORT_ERROR("Failed to allocate configuration source context, out of memory");
        return nullptr;
    }
    sourceContext->addLineData(1, str);

    ELogConfig* cfg = load(str, sourceContext);
    if (cfg == nullptr) {
        delete sourceContext;
        return nullptr;
    }

    // set context data and return
    cfg->m_sourceContext = sourceContext;
    return cfg;
}

ELogConfig* ELogConfig::loadFromProps(const ELogPropertyPosSequence& props) {
    // use empty context
    ELogConfigSourceContext* sourceContext = new (std::nothrow) ELogConfigSourceContext();
    if (sourceContext == nullptr) {
        ELOG_REPORT_ERROR("Failed to allocate configuration source context, out of memory");
        return nullptr;
    }

    ELogConfigContext* context = new (std::nothrow) ELogConfigContext(sourceContext, 0, "");
    if (sourceContext == nullptr) {
        ELOG_REPORT_ERROR("Failed to allocate configuration object context, out of memory");
        delete sourceContext;
        return nullptr;
    }

    ELogConfigMapNode* mapNode = new (std::nothrow) ELogConfigMapNode(context);
    if (mapNode == nullptr) {
        ELOG_REPORT_ERROR("Failed to allocate configuration map object, out of memory");
        delete context;
        delete sourceContext;
        return nullptr;
    }

    for (const auto& prop : props.m_sequence) {
        context = mapNode->makeConfigContext(prop.second->m_valuePos);
        if (context == nullptr) {
            ELOG_REPORT_ERROR("Failed to allocate configuration value context, out of memory");
            delete sourceContext;
            delete mapNode;
            return nullptr;
        }
        ELogConfigValue* value = loadValueFromProp(context, prop.first.c_str(), prop.second);
        if (value == nullptr) {
            ELOG_REPORT_ERROR(
                "Failed to load configuration from properties, invalid property %s type %u",
                prop.first.c_str(), (uint32_t)prop.second->m_type);
            delete sourceContext;
            delete mapNode;
            return nullptr;
        }
        mapNode->addEntry(prop.first.c_str(), value);
    }

    ELogConfig* cfg = new (std::nothrow) ELogConfig(mapNode);
    if (cfg == nullptr) {
        ELOG_REPORT_ERROR("Failed to allocate configuration object, out of memory");
        return nullptr;
    }
    cfg->m_sourceContext = sourceContext;

    // this should propagate the path to all sub-entities recursively
    cfg->m_root->setPathContext("<root>");
    return cfg;
}

ELogConfigValue* ELogConfig::loadValueFromProp(ELogConfigContext* context, const char* key,
                                               const ELogPropertyPos* prop) {
    ELogConfigValue* value = nullptr;
    if (prop->m_type == ELogPropertyType::PT_STRING) {
        // NOTE: value may be surrounded with single or double quotes
        const std::string& strValue = ((const ELogStringPropertyPos*)prop)->m_value;
        if ((strValue.front() == '"' && strValue.back() == '"') ||
            (strValue.front() == '\'' && strValue.back() == '\'')) {
            value = new (std::nothrow)
                ELogConfigStringValue(context, strValue.substr(1, strValue.size() - 2).c_str());
        } else {
            value = new (std::nothrow) ELogConfigStringValue(context, strValue.c_str());
        }
    } else if (prop->m_type == ELogPropertyType::PT_INT) {
        value = new (std::nothrow)
            ELogConfigIntValue(context, ((const ELogIntPropertyPos*)prop)->m_value);
    } else if (prop->m_type == ELogPropertyType::PT_BOOL) {
        value = new (std::nothrow)
            ELogConfigBoolValue(context, ((const ELogBoolPropertyPos*)prop)->m_value);
    } else {
        ELOG_REPORT_ERROR(
            "Failed to load configuration from properties, invalid property %s type %u", key,
            (uint32_t)prop->m_type);
    }
    return value;
}

bool ELogConfig::setSingleLineSourceContext(const char* line) {
    if (m_sourceContext != nullptr) {
        delete m_sourceContext;
    }
    m_sourceContext = new (std::nothrow) ELogConfigSourceContext();
    if (m_sourceContext == nullptr) {
        ELOG_REPORT_ERROR(
            "Failed to allocate memory for configuration source context, out of memory");
        return false;
    }
    m_sourceContext->addLineData(1, line);
    return true;
}

ELogConfig* ELogConfig::load(const char* str, ELogConfigSourceContext* sourceContext) {
    ELogStringTokenizer tok(str);
    ELogConfigNode* rootNode = parseConfigNode(tok, sourceContext);
    if (rootNode == nullptr) {
        ELOG_REPORT_ERROR("Failed to load configuration from: %s", str);
        return nullptr;
    }

    ELogConfig* cfg = new (std::nothrow) ELogConfig(rootNode);
    if (cfg == nullptr) {
        ELOG_REPORT_ERROR("Failed to allocate configuration object, out of memory");
        return nullptr;
    }
    // this should propagate the path to all sub-entities recursively
    cfg->m_root->setPathContext("<root>");
    return cfg;
}

static ELogConfigValue* parseSimpleValue(const std::string& token, ELogConfigContext* context) {
    ELogConfigValue* res = nullptr;
    // first take care of case of explicit quotes (string type)
    if (token.front() == '"') {
        if (token.back() != '"') {
            // parse as weird string, with only initial quote, warn about this
            ELOG_REPORT_WARN(
                "Ill-formed string value '%s' missing terminating quote, parsing as-is",
                token.c_str());
            res = new (std::nothrow) ELogConfigStringValue(context, token.c_str());
        } else {
            // shave-off quotes
            res = new (std::nothrow)
                ELogConfigStringValue(context, token.substr(1, token.length() - 2).c_str());
        }
    }

    // allow also for Javascript single quote style
    else if (token.front() == '\'') {
        if (token.back() != '\'') {
            // parse as weird string, with only initial quote, warn about this
            ELOG_REPORT_WARN(
                "Ill-formed string value '%s' missing terminating quote, parsing as-is",
                token.c_str());
            res = new (std::nothrow) ELogConfigStringValue(context, token.c_str());
        } else {
            // shave-off quotes
            res = new (std::nothrow)
                ELogConfigStringValue(context, token.substr(1, token.length() - 2).c_str());
        }
    }

    // simpler cases
    else if (token.compare("null") == 0) {
        res = new (std::nothrow) ELogConfigNullValue(context);
    } else if (token.compare("true") == 0 || token.compare("yes") == 0 ||
               token.compare("on") == 0) {
        res = new (std::nothrow) ELogConfigBoolValue(context, true);
    } else if (token.compare("false") == 0 || token.compare("no") == 0 ||
               token.compare("off") == 0) {
        res = new (std::nothrow) ELogConfigBoolValue(context, false);
    } else {
        // try to parse as integer
        int64_t value = 0;
        if (parseIntProp("", "", token, value, false)) {
            res = new (std::nothrow) ELogConfigIntValue(context, value);
        } else {
            res = new (std::nothrow) ELogConfigStringValue(context, token.c_str());
        }
    }

    if (res == nullptr) {
        ELOG_REPORT_ERROR("Failed to allocate configuration value, out of memory");
        return nullptr;
    }
    return res;
}

static ELogConfigValue* parseArrayValue(ELogStringTokenizer& tok,
                                        ELogConfigSourceContext* sourceContext,
                                        ELogConfigMode configMode) {
    ELogConfigNode* node = parseConfigNode(tok, sourceContext, configMode);
    if (node == nullptr) {
        ELOG_REPORT_ERROR("Failed to parse configuration array value");
        return nullptr;
    }
    if (node->getNodeType() != ELogConfigNodeType::ELOG_CONFIG_ARRAY_NODE) {
        ELOG_REPORT_ERROR("Unexpected result node type, expecting array, got instead %s",
                          configNodeTypeToString(node->getNodeType()));
        delete node;
        node = nullptr;
        return nullptr;
    }
    ELogConfigArrayNode* arrayNode = (ELogConfigArrayNode*)node;
    ELogConfigContext* context = node->makeConfigContext(node->getParsePos());
    if (context == nullptr) {
        ELOG_REPORT_ERROR("Failed to set parsed array value (context: %s)", node->getFullContext());
        delete context;
        delete node;
        return nullptr;
    }
    ELogConfigArrayValue* value = new (std::nothrow) ELogConfigArrayValue(context, arrayNode);
    if (value == nullptr) {
        ELOG_REPORT_ERROR("Failed to allocate configuration value, out of memory");
        delete context;
        delete node;
        node = nullptr;
        return nullptr;
    }
    value->setPathContext(node->getPathContext());
    return value;
}

static ELogConfigValue* parseMapValue(ELogStringTokenizer& tok,
                                      ELogConfigSourceContext* sourceContext,
                                      ELogConfigMode configMode) {
    ELogConfigNode* node = parseConfigNode(tok, sourceContext, configMode);
    if (node == nullptr) {
        ELOG_REPORT_ERROR("Failed to parse configuration array value");
        return nullptr;
    }
    if (node->getNodeType() != ELogConfigNodeType::ELOG_CONFIG_MAP_NODE) {
        ELOG_REPORT_ERROR("Unexpected result node type, expecting map, got instead %s",
                          configNodeTypeToString(node->getNodeType()));
        delete node;
        node = nullptr;
        return nullptr;
    }
    ELogConfigMapNode* mapNode = (ELogConfigMapNode*)node;
    ELogConfigContext* context = node->makeConfigContext(node->getParsePos());
    if (context == nullptr) {
        ELOG_REPORT_ERROR("Failed to set parsed map value (context: %s)", node->getFullContext());
        delete context;
        delete node;
        return nullptr;
    }
    ELogConfigMapValue* value = new (std::nothrow) ELogConfigMapValue(context, mapNode);
    if (value == nullptr) {
        ELOG_REPORT_ERROR("Failed to allocate configuration value, out of memory");
        delete context;
        delete node;
        node = nullptr;
        return nullptr;
    }
    value->setPathContext(node->getPathContext());
    return value;
}

static ELogConfigValue* parseConfigValue(ELogStringTokenizer& tok,
                                         ELogConfigSourceContext* sourceContext,
                                         ELogConfigMode configMode) {
    std::string token;
    ELogTokenType tokenType;
    uint32_t tokenPos = 0;

    // expecting either value, open brace or open brackets
    if (!tok.parseExpectedToken3(ELogTokenType::TT_TOKEN, ELogTokenType::TT_OPEN_BRACE,
                                 ELogTokenType::TT_OPEN_BRACKET, tokenType, token, tokenPos, "text",
                                 "'{'", "'['")) {
        return nullptr;
    }

    ELogConfigValue* res = nullptr;
    if ((tokenType == ELogTokenType::TT_OPEN_BRACE) ||
        (tokenType == ELogTokenType::TT_OPEN_BRACKET)) {
        // put back the open brace/bracket, and parse the nested configuration
        tok.rewind(tokenPos);
        res = (tokenType == ELogTokenType::TT_OPEN_BRACE)
                  ? parseMapValue(tok, sourceContext, configMode)
                  : parseArrayValue(tok, sourceContext, configMode);
    } else {
        ELogConfigContext* context =
            new (std::nothrow) ELogConfigContext(sourceContext, tokenPos, "");
        if (context == nullptr) {
            ELOG_REPORT_ERROR("Failed to allocate configuration entity context, out of memory");
            return nullptr;
        }
        res = parseSimpleValue(token, context);
        if (res == nullptr) {
            delete context;
            context = nullptr;
        }
    }

    return res;
}

enum ParseState {
    PS_INIT,
    PS_KEY,
    PS_VALUE,
    PS_KEY_RES,
    PS_BRACKET,
    PS_BRACE,
    PS_BRACKET_VALUE,
    PS_BRACE_KEY,
    PS_BRACE_VALUE,
    PS_DONE
};

ELogConfigNode* parseConfigNode(ELogStringTokenizer& tok, ELogConfigSourceContext* sourceContext,
                                ELogConfigMode configMode /* = ELogConfigMode::CM_CONSISTENT */) {
    ELogConfigNode* res = nullptr;
    ELogConfigArrayNode* arrayRes = nullptr;
    ELogConfigMapNode* mapRes = nullptr;
    ELogConfigValue* value = nullptr;
    std::string token;
    std::string key;
    ELogTokenType tokenType;
    uint32_t tokenPos = 0;

    ParseState parseState = PS_INIT;
    ParseState nextState = PS_DONE;

    while (tok.hasMoreTokens() && parseState != PS_DONE) {
        // parse state: INIT
        if (parseState == PS_INIT) {
            // first token must be open brace, open bracket or a simple text token
            if (!tok.parseExpectedToken3(ELogTokenType::TT_OPEN_BRACE,
                                         ELogTokenType::TT_OPEN_BRACKET, ELogTokenType::TT_TOKEN,
                                         tokenType, key, tokenPos, "'{'", "'[", "text")) {
                break;
            }

            // move to next state
            if (tokenType == ELogTokenType::TT_OPEN_BRACE) {
                parseState = PS_BRACE;
            } else if (tokenType == ELogTokenType::TT_OPEN_BRACKET) {
                parseState = PS_BRACKET;
            } else if (tokenType == ELogTokenType::TT_TOKEN) {
                parseState = PS_KEY;
            } else {
                assert(false);
            }
        }

        // parse state: KEY
        else if (parseState == PS_KEY) {
            // expecting equal sign
            if (configMode == ELogConfigMode::CM_EQUALS) {
                if (!tok.parseExpectedToken(ELogTokenType::TT_EQUAL_SIGN, token, "'='")) {
                    break;
                }
            } else if (configMode == ELogConfigMode::CM_COLON) {
                if (!tok.parseExpectedToken(ELogTokenType::TT_COLON_SIGN, token, "':'")) {
                    break;
                }
            } else if ((configMode == ELogConfigMode::CM_ANY) ||
                       (configMode == ELogConfigMode::CM_CONSISTENT)) {
                if (!tok.parseExpectedToken2(ELogTokenType::TT_EQUAL_SIGN,
                                             ELogTokenType::TT_COLON_SIGN, tokenType, token,
                                             tokenPos, "'=", "':'")) {
                    break;
                }
                // in case of consistent mode, we need to evolve to either equals or colon mode
                if (configMode == ELogConfigMode::CM_CONSISTENT) {
                    if (tokenType == ELogTokenType::TT_EQUAL_SIGN) {
                        configMode = ELogConfigMode::CM_EQUALS;
                    } else {
                        assert(tokenType == ELogTokenType::TT_COLON_SIGN);
                        configMode = ELogConfigMode::CM_COLON;
                    }
                }
            }
            // move to state PS_VALUE, make sure next state is PS_KEY_RES
            nextState = PS_KEY_RES;
            parseState = PS_VALUE;
        }

        // parse state: VALUE
        else if (parseState == PS_VALUE) {
            // parse value according to token type
            value = parseConfigValue(tok, sourceContext, configMode);
            if (value == nullptr) {
                ELOG_REPORT_ERROR("Failed to parse value for key %s", key.c_str());
                break;
            }

            // move to next state as decided by previous state
            parseState = nextState;
            nextState = PS_DONE;
        }

        // parse state: KEY RES
        else if (parseState == PS_KEY_RES) {
            // collect value
            assert(value != nullptr);
            nextState = PS_DONE;

            // return a single mapping to caller
            ELogConfigContext* context =
                new (std::nothrow) ELogConfigContext(sourceContext, tokenPos, "");
            if (context == nullptr) {
                ELOG_REPORT_ERROR("Failed to allocate configuration entity context, out of memory");
                break;
            }
            res = new (std::nothrow) ELogConfigSimpleNode(context, key.c_str(), value);
            if (res == nullptr) {
                ELOG_REPORT_ERROR("Failed to allocate configuration node for key %s, out of memory",
                                  key.c_str());
                delete context;
                break;
            }
            value = nullptr;
            parseState = PS_DONE;
        }

        // parse state: BRACKET
        else if (parseState == PS_BRACKET) {
            // we just parsed open bracket, so we expect a sequence of configuration items until
            // close bracket is seen. we peek for next token, which should be either close bracket
            // or another array item. empty array is possible

            // we first create the array on demand
            if (arrayRes == nullptr) {
                ELogConfigContext* context =
                    new (std::nothrow) ELogConfigContext(sourceContext, tokenPos, "");
                if (context == nullptr) {
                    ELOG_REPORT_ERROR(
                        "Failed to allocate configuration entity context, out of memory");
                    break;
                }
                arrayRes = new (std::nothrow) ELogConfigArrayNode(context);
                if (arrayRes == nullptr) {
                    ELOG_REPORT_ERROR("Failed to allocate configuration array, out of memory");
                    delete context;
                    break;
                }
            }

            if (tok.peekNextTokenType() == ELogTokenType::TT_CLOSE_BRACKET) {
                // empty array
                res = arrayRes;
                arrayRes = nullptr;
                parseState = PS_DONE;
            } else {
                nextState = PS_BRACKET_VALUE;
                parseState = PS_VALUE;
            }
        }

        // parse state: BRACKET VALUE
        else if (parseState == PS_BRACKET_VALUE) {
            // collect value
            assert(value != nullptr);
            assert(arrayRes != nullptr);
            arrayRes->addValue(value);
            value = nullptr;

            // we just parsed value in an array, so we expect either a comma or a close bracket
            if (!tok.parseExpectedToken2(ELogTokenType::TT_COMMA, ELogTokenType::TT_CLOSE_BRACKET,
                                         tokenType, token, tokenPos, "','", "']")) {
                ELOG_REPORT_ERROR("Unexpected token while parseing configuration array");
                break;
            }
            if (tokenType == ELogTokenType::TT_CLOSE_BRACKET) {
                res = arrayRes;
                arrayRes = nullptr;
                parseState = PS_DONE;
            } else {
                nextState = PS_BRACKET_VALUE;
                parseState = PS_VALUE;
            }
        }

        // parse state: BRACE
        else if (parseState == PS_BRACE) {
            // we just parsed open bracket, so we expect a sequence of configuration items until
            // close bracket is seen. we peek for next token, which should be either close bracket
            // or another array item. empty array is possible

            // we first create the array on demand
            if (mapRes == nullptr) {
                ELogConfigContext* context =
                    new (std::nothrow) ELogConfigContext(sourceContext, tokenPos, "");
                if (context == nullptr) {
                    ELOG_REPORT_ERROR(
                        "Failed to allocate configuration entity context, out of memory");
                    break;
                }
                mapRes = new (std::nothrow) ELogConfigMapNode(context);
                if (mapRes == nullptr) {
                    ELOG_REPORT_ERROR("Failed to allocate configuration map, out of memory");
                    delete context;
                    break;
                }
            }

            if (tok.peekNextTokenType() == ELogTokenType::TT_CLOSE_BRACE) {
                // empty map
                res = mapRes;
                mapRes = nullptr;
                parseState = PS_DONE;
            } else {
                parseState = PS_BRACE_KEY;
            }
        }

        // parse state: BRACE KEY
        else if (parseState == PS_BRACE_KEY) {
            // parse key
            if (!tok.parseExpectedToken(ELogTokenType::TT_TOKEN, key, "text")) {
                ELOG_REPORT_ERROR("Unexpected token while parseing configuration array");
                break;
            }

            // expecting equal sign
            if (!tok.parseExpectedToken(ELogTokenType::TT_EQUAL_SIGN, token, "'='")) {
                break;
            }

            // move to state PS_BRACE_VALUE, make sure next state is PS_KEY_RES
            parseState = PS_VALUE;
            nextState = PS_BRACE_VALUE;
        }

        // parse state: BRACE VALUE
        else if (parseState == PS_BRACE_VALUE) {
            // collect key, value pair
            assert(!key.empty());
            assert(value != nullptr);
            assert(mapRes != nullptr);
            if (!mapRes->addEntry(key.c_str(), value)) {
                ELOG_REPORT_ERROR("Duplicate key %s encountered while parsing configuration map",
                                  key.c_str());
                break;
            }
            key.clear();
            value = nullptr;

            // we just parsed value in a map, so we expect either a comma or a close brace
            if (!tok.parseExpectedToken2(ELogTokenType::TT_COMMA, ELogTokenType::TT_CLOSE_BRACE,
                                         tokenType, token, tokenPos, "','", "'}")) {
                ELOG_REPORT_ERROR("Unexpected token while parseing configuration map");
                break;
            }
            if (tokenType == ELogTokenType::TT_CLOSE_BRACE) {
                res = mapRes;
                mapRes = nullptr;
                parseState = PS_DONE;
            } else {
                parseState = PS_BRACE_KEY;
            }
        }

        // invalid parse state
        else {
            ELOG_REPORT_ERROR(
                "Internal error: Invalid parse state %u while parsing nested log target "
                "specification",
                (unsigned)parseState);
            break;
        }
    }

    if (parseState != PS_DONE) {
        ELOG_REPORT_ERROR("Premature end of configuration stream at pos %u: %s", tokenPos,
                          tok.getSourceStr());
        if (value != nullptr) {
            delete value;
            value = nullptr;
        }
        if (arrayRes != nullptr) {
            delete arrayRes;
            arrayRes = nullptr;
        }
        if (mapRes != nullptr) {
            delete mapRes;
            mapRes = nullptr;
        }
        return nullptr;
    }

    assert(res != nullptr);
    return res;
}

}  // namespace elog
