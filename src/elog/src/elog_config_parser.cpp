#include "elog_config_parser.h"

#include <algorithm>
#include <cstring>

#include "elog_async_schema_handler.h"
#include "elog_common.h"
#include "elog_error.h"
#include "elog_schema_manager.h"
#include "elog_system.h"

namespace elog {

bool ELogConfigParser::parseLogLevel(const char* logLevelStr, ELogLevel& logLevel,
                                     ELogPropagateMode& propagateMode) {
    const char* ptr = nullptr;
    if (!elogLevelFromStr(logLevelStr, logLevel, &ptr)) {
        ELOG_REPORT_ERROR("Invalid log level: %s", logLevelStr);
        return false;
    }

    // parse optional propagation sign if there is any
    propagateMode = ELogPropagateMode::PM_NONE;
    uint32_t parseLen = ptr - logLevelStr;
    uint32_t len = strlen(logLevelStr);
    if (parseLen < len) {
        // there are more chars, only one is allowed
        if (parseLen + 1 != len) {
            ELOG_REPORT_ERROR(
                "Invalid excess chars at global log level: %s (only one character is allowed: '*', "
                "'+' or '-')",
                logLevelStr);
            return false;
        } else if (*ptr == '*') {
            propagateMode = ELogPropagateMode::PM_SET;
        } else if (*ptr == '-') {
            propagateMode = ELogPropagateMode::PM_RESTRICT;
        } else if (*ptr == '+') {
            propagateMode = ELogPropagateMode::PM_LOOSE;
        } else {
            ELOG_REPORT_ERROR(
                "Invalid excess chars at global log level: %s (only one character is allowed: '*', "
                "'+' or '-')",
                logLevelStr);
            return false;
        }
    }

    return true;
}

bool ELogConfigParser::parseLogAffinityList(const char* affinityListStr,
                                            ELogTargetAffinityMask& mask) {
    // tokenize target names by comma
    mask = 0;
    ELogStringTokenizer tokenizer(affinityListStr);
    ELogTokenType prevTokenType = ELogTokenType::TT_COMMA;
    while (tokenizer.hasMoreTokens()) {
        ELogTokenType tokenType = ELogTokenType::TT_TOKEN;
        std::string token;
        uint32_t tokenPos = 0;
        if (!tokenizer.nextToken(tokenType, token, tokenPos)) {
            ELOG_REPORT_ERROR("Failed to parse log target list: %s",
                              tokenizer.getErrLocStr(tokenPos).c_str());
            return false;
        }
        if (tokenType != ELogTokenType::TT_TOKEN && tokenType != ELogTokenType::TT_COMMA) {
            ELOG_REPORT_ERROR(
                "Unexpected token '%s' in log target list, should be either log target name or "
                "comma",
                token.c_str());
            return false;
        }
        if (tokenType == prevTokenType) {
            if (tokenType == ELogTokenType::TT_TOKEN) {
                ELOG_REPORT_ERROR("Missing comma in log target list: %s",
                                  tokenizer.getErrLocStr(tokenPos));
            } else {
                ELOG_REPORT_ERROR("Duplicate comma in log target list: %s",
                                  tokenizer.getErrLocStr(tokenPos));
            }
            return false;
        }
        ELogTargetId logTargetId = ELogSystem::getLogTargetId(token.c_str());
        if (logTargetId == ELOG_INVALID_TARGET_ID) {
            ELOG_REPORT_ERROR("Invalid log target list, unknown log target '%s", token.c_str());
            return false;
        }
        ELOG_ADD_TARGET_AFFINITY_MASK(mask, logTargetId);
    }
    return true;
}

bool ELogConfigParser::parseLogTargetSpec(const std::string& logTargetCfg,
                                          ELogTargetNestedSpec& logTargetNestedSpec,
                                          ELogTargetSpecStyle& specStyle) {
    specStyle = ELogTargetSpecStyle::ELOG_STYLE_URL;
    if (logTargetCfg.starts_with("{") || logTargetCfg.starts_with("[")) {
        if (!parseLogTargetNestedSpec(logTargetCfg, logTargetNestedSpec)) {
            ELOG_REPORT_ERROR("Invalid log target specification: %s", logTargetCfg.c_str());
            return false;
        }
        specStyle = ELogTargetSpecStyle::ELOG_STYLE_NESTED;
    } else if (!parseLogTargetSpec(logTargetCfg, logTargetNestedSpec.m_spec)) {
        ELOG_REPORT_ERROR("Invalid log target specification: %s", logTargetCfg.c_str());
        return false;
    }
    return true;
}

ELogConfig* ELogConfigParser::parseLogTargetConfig(const std::string& logTargetUrl) {
    // first parse the url, then convert to configuration object
    // NOTE: if asynchronous log target specification is embedded, it is now done through the
    // fragment part of the URL (which can theoretically be repeated)
    ELogTargetUrlSpec urlSpec;
    if (!parseLogTargetUrl(logTargetUrl, urlSpec)) {
        return nullptr;
    }

    // TODO: we need to prepare a configuration object
    // normally this would be a simple single-level map node, but in case there is an asynchronous
    // target specification (i.e. deferred, queued, quantum, etc.), in which case we get a two-level
    // configuration object, so we first parse the URL as scheme://path?key=value&key=value...
    // that is to say, a simple key/value map. Next we should check for asynchronous target keys,
    // and if found, build a two-level configuration. the only caveat here is to allow for the async
    // schema to infer by itself whether this is an asynchronous target URL
    // since this design is complicated, not generic (what is async schema rules change?), it was
    // decided to put any sub-log target specification in the fragment part of the URL. Although
    // this is not an orthodox solution, this choice currently preferred

    // each level should be converted to a map node, each sub-level should be linked to a log_target
    // property, the process is iterative, descending as deep as needed. finally we link the top
    // level map node as the root node for the configuration. The only thing we miss is property
    // locations, so this needs to be embedded within the URL spec.

    ELogConfig* config = new (std::nothrow) ELogConfig();
    if (config == nullptr) {
        ELOG_REPORT_ERROR("Failed to allocate configuration map value, out of memory");
        return nullptr;
    }
    if (!config->setSingleLineSourceContext(logTargetUrl.c_str())) {
        delete config;
        return nullptr;
    }

    ELogConfigMapNode* mapNode = logTargetUrlToConfig(&urlSpec, config->getSourceContext());
    if (mapNode == nullptr) {
        delete config;
        return nullptr;
    }
    ELogConfigMapNode* rootNode = mapNode;
    config->setRootNode(rootNode);
    ELogTargetUrlSpec* subUrl = urlSpec.m_subUrlSpec;
    while (subUrl != nullptr) {
        ELogConfigMapNode* subMapNode = logTargetUrlToConfig(subUrl, config->getSourceContext());
        if (subMapNode == nullptr) {
            delete config;
            return nullptr;
        }
        ELogConfigContext* context = subMapNode->makeConfigContext(subMapNode->getParsePos());
        if (context == nullptr) {
            delete subMapNode;
            delete config;
            return nullptr;
        }
        ELogConfigMapValue* subMapValue =
            new (std::nothrow) ELogConfigMapValue(context, subMapNode);
        if (subMapValue == nullptr) {
            ELOG_REPORT_ERROR("Failed to allocate configuration map value, out of memory");
            delete subMapNode;
            delete config;
            return nullptr;
        }
        if (!mapNode->addEntry("log_target", subMapValue)) {
            delete subMapNode;
            delete config;
            return nullptr;
        }
        mapNode = subMapNode;
        subUrl = subUrl->m_subUrlSpec;
    }
    return config;
}

bool ELogConfigParser::parseHostPort(const std::string& server, std::string& host, int& port) {
    std::string::size_type colonPos = server.find(':');
    if (colonPos == std::string::npos) {
        ELOG_REPORT_ERROR("Server specification missing colon: %s", server.c_str());
        return false;
    }
    if (!parseIntProp("port", "", server.substr(colonPos + 1), port, false)) {
        return false;
    }
    host = server.substr(0, colonPos);
    return true;
}

bool ELogConfigParser::parseLogTargetSpec(const std::string& logTargetCfg,
                                          ELogTargetSpec& logTargetSpec) {
    // find scheme separator
    std::string::size_type schemeSepPos = logTargetCfg.find(ELogSchemaManager::ELOG_SCHEMA_MARKER);
    if (schemeSepPos == std::string::npos) {
        ELOG_REPORT_ERROR("Invalid log target specification, missing scheme separator \'%s\': %s",
                          ELogSchemaManager::ELOG_SCHEMA_MARKER, logTargetCfg.c_str());
        return false;
    }

    logTargetSpec.m_scheme = logTargetCfg.substr(0, schemeSepPos);

    // parse until first '?'
    std::string::size_type qmarkPos =
        logTargetCfg.find('?', schemeSepPos + ELogSchemaManager::ELOG_SCHEMA_LEN);
    if (qmarkPos == std::string::npos) {
        logTargetSpec.m_path =
            logTargetCfg.substr(schemeSepPos + ELogSchemaManager::ELOG_SCHEMA_LEN);
        logTargetSpec.m_port = 0;
        tryParsePathAsHostPort(logTargetCfg, logTargetSpec);
        return true;
    }

    logTargetSpec.m_path =
        logTargetCfg.substr(schemeSepPos + ELogSchemaManager::ELOG_SCHEMA_LEN,
                            qmarkPos - schemeSepPos - ELogSchemaManager::ELOG_SCHEMA_LEN);
    tryParsePathAsHostPort(logTargetCfg, logTargetSpec);

    // parse properties, separated by ampersand
    std::string::size_type prevPos = qmarkPos + 1;
    std::string::size_type sepPos = logTargetCfg.find('&', prevPos);
    do {
        // get property
        std::string prop = (sepPos == std::string::npos)
                               ? logTargetCfg.substr(prevPos)
                               : logTargetCfg.substr(prevPos, sepPos - prevPos);

        // parse to key=value and add to props map (could be there is no value specified)
        std::string::size_type equalPos = prop.find('=');
        if (equalPos != std::string::npos) {
            std::string key = prop.substr(0, equalPos);
            std::string value = prop.substr(equalPos + 1);
            insertPropOverride(logTargetSpec.m_props, key, value);
        } else {
            insertPropOverride(logTargetSpec.m_props, prop, "");
        }

        // find next token separator
        if (sepPos != std::string::npos) {
            prevPos = sepPos + 1;
            sepPos = logTargetCfg.find('&', prevPos);
        } else {
            prevPos = sepPos;
        }
    } while (prevPos != std::string::npos);
    return true;
}

bool ELogConfigParser::parseLogTargetNestedSpec(const std::string& logTargetCfg,
                                                ELogTargetNestedSpec& logTargetNestedSpec) {
    // we need to parse this recursively as a stream:
    // 1. whenever seeing an opening brace we descend to recursive call
    // 2. whenever seeing a closing brace we return from recursive call
    // 3. otherwise we parse prop = value, and look carefully that value does not begin with an
    // opening brace.
    // 4. each nested call expected first char to be a curly open brace

    // it is much easier with a tokenizer, a token stream, and parse state machine...
    ELogStringTokenizer tok(logTargetCfg);
    if (!parseLogTargetNestedSpec(logTargetCfg, logTargetNestedSpec, tok)) {
        return false;
    }
    if (tok.hasMoreTokens()) {
        ELOG_REPORT_ERROR(
            "Excess characters after position %u not parsed in log target specification: %s",
            tok.getPos(), logTargetCfg.c_str());
        return false;
    }
    return true;
}

bool ELogConfigParser::parseLogTargetNestedSpec(const std::string& logTargetCfg,
                                                ELogTargetNestedSpec& logTargetNestedSpec,
                                                ELogStringTokenizer& tok) {
    std::string token;

    // first token must be open brace
    if (!tok.parseExpectedToken(ELogTokenType::TT_OPEN_BRACE, token, "'{'")) {
        return false;
    }

    std::string key;
    ELogTokenType tokenType;
    uint32_t tokenPos = 0;
    enum PraseState {
        PS_INIT,
        PS_KEY,
        PS_EQUAL,
        PS_BRACKET,
        PS_VALUE,
        PS_DONE
    } parseState = PS_INIT;

    while (tok.hasMoreTokens() && parseState != PS_DONE) {
        // parse state: INIT
        if (parseState == PS_INIT) {
            // either a key or a close brace
            if (!tok.parseExpectedToken2(ELogTokenType::TT_TOKEN, ELogTokenType::TT_CLOSE_BRACE,
                                         tokenType, key, tokenPos, "text", "'}'")) {
                return false;
            }
            // move to next state
            parseState = (tokenType == ELogTokenType::TT_CLOSE_BRACE) ? PS_DONE : PS_KEY;
        }

        // parse state: KEY
        else if (parseState == PS_KEY) {
            // expecting equal sign
            if (!tok.parseExpectedToken(ELogTokenType::TT_EQUAL_SIGN, token, "'='")) {
                return false;
            }
            // move to state PS_EQUAL
            parseState = PS_EQUAL;
        }

        // parse state: EQUAL
        else if (parseState == PS_EQUAL) {
            // expecting either value, open brace or open brackets
            if (!tok.parseExpectedToken3(ELogTokenType::TT_TOKEN, ELogTokenType::TT_OPEN_BRACE,
                                         ELogTokenType::TT_OPEN_BRACKET, tokenType, token, tokenPos,
                                         "text", "'{'", "'['")) {
                return false;
            }
            // if open brace, then make recursive parsing, after which move to state PS_VALUE
            // if open bracket, then make recursive parsing, but next expected is PS_BRACKET
            // if value then move to state PS_VALUE
            if (tokenType == ELogTokenType::TT_OPEN_BRACE) {
                // insert new entry for sub-spec in sub-spec map
                std::pair<ELogTargetNestedSpec::SubSpecMap::iterator, bool> itrRes =
                    logTargetNestedSpec.m_subSpec.insert(
                        ELogTargetNestedSpec::SubSpecMap::value_type(
                            key, ELogTargetNestedSpec::SubSpecList(1)));
                if (!itrRes.second) {
                    ELOG_REPORT_ERROR("Duplicate nested key '%s' at pos %u", key.c_str(), tokenPos);
                    ELOG_REPORT_ERROR("Error location: %s", tok.getErrLocStr(tokenPos).c_str());
                    return false;
                }
                // put back the open brace, parse the sub-spec
                tok.rewind(tokenPos);
                if (!parseLogTargetNestedSpec(logTargetCfg, itrRes.first->second[0], tok)) {
                    ELOG_REPORT_ERROR("Failed to parse nested log target specification");
                    return false;
                }
                parseState = PS_VALUE;
            } else if (tokenType == ELogTokenType::TT_OPEN_BRACKET) {
                // start of log target spec array [{}, {}, ...]
                // parse sub-spec and stay in BRACKET state
                // but first insert new entry in sub-spec map with one array item
                std::pair<ELogTargetNestedSpec::SubSpecMap::iterator, bool> itrRes =
                    logTargetNestedSpec.m_subSpec.insert(
                        ELogTargetNestedSpec::SubSpecMap::value_type(
                            key, ELogTargetNestedSpec::SubSpecList(1)));
                if (!itrRes.second) {
                    ELOG_REPORT_ERROR("Duplicate nested key '%s' at pos %u", key.c_str(), tokenPos);
                    ELOG_REPORT_ERROR("Error location: %s", tok.getErrLocStr(tokenPos).c_str());
                    return false;
                }
                // parse the sub-spec into the first array item
                if (!parseLogTargetNestedSpec(logTargetCfg, itrRes.first->second[0], tok)) {
                    ELOG_REPORT_ERROR("Failed to parse nested log target specification");
                    return false;
                }
                parseState = PS_BRACKET;
            } else {
                // add value to property map
                std::pair<ELogPropertyMap::iterator, bool> itrRes =
                    logTargetNestedSpec.m_spec.m_props.insert(
                        ELogPropertyMap::value_type(key, token));
                if (!itrRes.second) {
                    // override existing key-value mappings
                    itrRes.first->second = token;
                }
                parseState = PS_VALUE;
            }
        }

        // parse state: BRACKET
        else if (parseState == PS_BRACKET) {
            // expected either a comma (followed by another sub-spec) or end bracket
            if (!tok.parseExpectedToken2(ELogTokenType::TT_COMMA, ELogTokenType::TT_CLOSE_BRACKET,
                                         tokenType, key, tokenPos, "','", "']'")) {
                return false;
            }
            // if close bracket then move to state PS_VALUE
            // if comma, then parse sub-spec and stay in state PS_BRACKET
            if (tokenType == ELogTokenType::TT_CLOSE_BRACKET) {
                parseState = PS_VALUE;
            } else {
                // search for the nested spec by the current parsed key
                ELogTargetNestedSpec::SubSpecMap::iterator itr =
                    logTargetNestedSpec.m_subSpec.find(key);
                if (itr == logTargetNestedSpec.m_subSpec.end()) {
                    ELOG_REPORT_ERROR("Internal error, missing sub-spec array key '%s' at pos %u",
                                      key.c_str(), tokenPos);
                    return false;
                }
                // now add a new item to the array, and parse the sub-spec into the new item
                ELogTargetNestedSpec::SubSpecList& subSpecList = itr->second;
                subSpecList.emplace_back(ELogTargetNestedSpec());
                if (!parseLogTargetNestedSpec(logTargetCfg, subSpecList.back(), tok)) {
                    ELOG_REPORT_ERROR("Failed to parse nested log target specification");
                    return false;
                }
                parseState = PS_BRACKET;
            }
        }

        // parse state: VALUE
        else if (parseState == PS_VALUE) {
            // expecting comma or close brace
            if (!tok.parseExpectedToken2(ELogTokenType::TT_COMMA, ELogTokenType::TT_CLOSE_BRACE,
                                         tokenType, key, tokenPos, "','", "'}'")) {
                return false;
            }
            // if close brace then exit
            // if comman then move to state PS_INIT
            if (tokenType == ELogTokenType::TT_CLOSE_BRACE) {
                parseState = PS_DONE;
            } else {
                parseState = PS_INIT;
            }
        }

        // invalid parse state
        else {
            ELOG_REPORT_ERROR(
                "Internal error: Invalid parse state %u while parsing nested log target "
                "specification",
                (unsigned)parseState);
            return false;
        }
    }

    if (parseState != PS_DONE) {
        ELOG_REPORT_ERROR("Premature end of nested log target specification at pos %u: %s",
                          tokenPos, logTargetCfg.c_str());
        return false;
    }

    // apply common members if there are such
    ELogPropertyMap::iterator itr = logTargetNestedSpec.m_spec.m_props.find("scheme");
    if (itr != logTargetNestedSpec.m_spec.m_props.end()) {
        logTargetNestedSpec.m_spec.m_scheme = itr->second;
    }
    itr = logTargetNestedSpec.m_spec.m_props.find("path");
    if (itr != logTargetNestedSpec.m_spec.m_props.end()) {
        logTargetNestedSpec.m_spec.m_path = itr->second;
        tryParsePathAsHostPort(logTargetCfg, logTargetNestedSpec.m_spec);
    }

    return true;
}

void ELogConfigParser::insertPropOverride(ELogPropertyMap& props, const std::string& key,
                                          const std::string& value) {
    std::pair<ELogPropertyMap::iterator, bool> itrRes =
        props.insert(ELogPropertyMap::value_type(key, value));
    if (!itrRes.second) {
        itrRes.first->second = value;
    }
}

void ELogConfigParser::tryParsePathAsHostPort(const std::string& logTargetCfg,
                                              ELogTargetSpec& logTargetSpec) {
    std::string::size_type colonPos = logTargetSpec.m_path.find(':');
    if (colonPos != std::string::npos) {
        if (!parseIntProp("port", logTargetCfg, logTargetSpec.m_path.substr(colonPos + 1),
                          logTargetSpec.m_port, false)) {
            return;
        }
        logTargetSpec.m_host = logTargetSpec.m_path.substr(0, colonPos);
    }
}

bool ELogConfigParser::parseLogTargetUrl(const std::string& logTargetUrl,
                                         ELogTargetUrlSpec& logTargetUrlSpec,
                                         uint32_t basePos /* = 0 */) {
    // first parse the url, then convert to configuration object
    // since we may need to specify a sub-target we use the fragment part to specify a sub-target
    ELogTargetUrlSpec* subTargetUrlSpec = nullptr;
    std::string::size_type hashPos = logTargetUrl.find('#');
    if (hashPos != std::string::npos) {
        if (!parseLogTargetUrl(logTargetUrl.substr(0, hashPos), logTargetUrlSpec, basePos)) {
            ELOG_REPORT_ERROR("Failed to parse top-level log target RUL specification");
            return false;
        }
        logTargetUrlSpec.m_subUrlSpec = new (std::nothrow) ELogTargetUrlSpec();
        if (logTargetUrlSpec.m_subUrlSpec == nullptr) {
            ELOG_REPORT_ERROR("Failed to allocate log target URL object, out of memory");
            return false;
        }
        if (!parseLogTargetUrl(logTargetUrl.substr(hashPos + 1), *logTargetUrlSpec.m_subUrlSpec,
                               basePos + hashPos + 1)) {
            ELOG_REPORT_ERROR("Failed to parse sub-log target URL specification");
            delete logTargetUrlSpec.m_subUrlSpec;
            logTargetUrlSpec.m_subUrlSpec = nullptr;
            return false;
        }
        return true;
    }

    // find scheme separator
    logTargetUrlSpec.m_subUrlSpec = nullptr;
    std::string::size_type schemeSepPos = logTargetUrl.find(ELogSchemaManager::ELOG_SCHEMA_MARKER);
    if (schemeSepPos == std::string::npos) {
        ELOG_REPORT_ERROR(
            "Invalid log target URL specification, missing scheme separator \'%s\': %s",
            ELogSchemaManager::ELOG_SCHEMA_MARKER, logTargetUrl.c_str());
        return false;
    }
    logTargetUrlSpec.m_scheme.m_value = logTargetUrl.substr(0, schemeSepPos);
    logTargetUrlSpec.m_scheme.m_keyPos = basePos + schemeSepPos;
    logTargetUrlSpec.m_scheme.m_valuePos = basePos + schemeSepPos;

    // parse until first '?'
    std::string::size_type pathPos = schemeSepPos + ELogSchemaManager::ELOG_SCHEMA_LEN;
    logTargetUrlSpec.m_path.m_keyPos = basePos + pathPos;
    logTargetUrlSpec.m_path.m_valuePos = basePos + pathPos;
    std::string::size_type qmarkPos = logTargetUrl.find('?', pathPos);
    if (qmarkPos == std::string::npos) {
        logTargetUrlSpec.m_path.m_value = logTargetUrl.substr(pathPos);
        return parseUrlPath(logTargetUrlSpec, basePos);
    }

    logTargetUrlSpec.m_path.m_value = logTargetUrl.substr(pathPos, qmarkPos - pathPos);
    if (!parseUrlPath(logTargetUrlSpec, basePos)) {
        return false;
    }

    // parse properties, separated by ampersand
    std::string::size_type prevPos = qmarkPos + 1;
    std::string::size_type sepPos = logTargetUrl.find('&', prevPos);
    do {
        // get property
        std::string prop = (sepPos == std::string::npos)
                               ? logTargetUrl.substr(prevPos)
                               : logTargetUrl.substr(prevPos, sepPos - prevPos);
        uint32_t keyPos = basePos + prevPos;

        // parse to key=value and add to props map (could be there is no value specified)
        std::string::size_type equalPos = prop.find('=');
        if (equalPos != std::string::npos) {
            std::string key = trim(prop.substr(0, equalPos));
            std::string value = trim(prop.substr(equalPos + 1));
            uint32_t valuePos = keyPos + equalPos + 1;
            // take care of pre-defined properties: user, password, host port
            if ((key.compare("user") == 0) || (key.compare("userName") == 0) ||
                (key.compare("user_name") == 0)) {
                logTargetUrlSpec.m_user.m_value = {value, keyPos, valuePos};
            } else if ((key.compare("password") == 0) || (key.compare("passwd") == 0)) {
                logTargetUrlSpec.m_passwd.m_value = {value, keyPos, valuePos};
            } else if ((key.compare("host") == 0) || (key.compare("hostName") == 0) ||
                       (key.compare("host_name") == 0)) {
                logTargetUrlSpec.m_host.m_value = {value, keyPos, valuePos};
            } else if ((key.compare("port") == 0) || (key.compare("portNumber") == 0) ||
                       (key.compare("port_number") == 0)) {
                if (!parseIntProp(key.c_str(), logTargetUrl, value, logTargetUrlSpec.m_port.m_value,
                                  false)) {
                    ELOG_REPORT_WARN(
                        "Failed to parse log target URL specification property %s=%s as port "
                        "number (context: %s)",
                        key.c_str(), value.c_str(), logTargetUrl.c_str());
                }
                logTargetUrlSpec.m_port.m_keyPos = keyPos;
                logTargetUrlSpec.m_port.m_valuePos = valuePos;
            }
            insertPropPosOverride(logTargetUrlSpec.m_props, key, value, keyPos, valuePos);
        } else {
            insertPropPosOverride(logTargetUrlSpec.m_props, trim(prop), "", keyPos, 0);
        }

        // find next token separator
        if (sepPos != std::string::npos) {
            prevPos = sepPos + 1;
            sepPos = logTargetUrl.find('&', prevPos);
        } else {
            prevPos = sepPos;
        }
    } while (prevPos != std::string::npos);

    return true;
}

bool ELogConfigParser::parseUrlPath(ELogTargetUrlSpec& logTargetUrlSpec, uint32_t basePos) {
    // the path may be specified as follows: authority/path
    // the authority may be specified as follows: [userinfo "@"] host [":" port]
    // and user info is: [user[:password]
    // so we first search for a slash, and anything preceding it is the authority part
    std::string::size_type slashPos = logTargetUrlSpec.m_path.m_value.find('/');
    if (slashPos == std::string::npos || slashPos == 0) {
        return true;
    }

    // user/password
    std::string authority = logTargetUrlSpec.m_path.m_value.substr(0, slashPos);
    logTargetUrlSpec.m_path.m_value = logTargetUrlSpec.m_path.m_value.substr(slashPos + 1);
    std::string::size_type atPos = authority.find('@');
    if (atPos != std::string::npos) {
        std::string userPass = authority.substr(0, atPos);
        logTargetUrlSpec.m_user.m_keyPos = basePos + slashPos;
        logTargetUrlSpec.m_user.m_valuePos = basePos + slashPos;

        // check for password
        std::string::size_type colonPos = userPass.find(':');
        logTargetUrlSpec.m_user.m_value = userPass.substr(0, colonPos);  // ok also if not found
        if (colonPos != std::string::npos) {
            logTargetUrlSpec.m_passwd.m_value = userPass.substr(colonPos + 1);
            logTargetUrlSpec.m_passwd.m_keyPos = basePos + colonPos + 1;
            logTargetUrlSpec.m_passwd.m_valuePos = basePos + colonPos + 1;
        }
    }

    // host/port
    std::string hostPort = (atPos != std::string::npos) ? authority.substr(atPos + 1) : authority;
    logTargetUrlSpec.m_host.m_keyPos = (atPos != std::string::npos) ? basePos + atPos + 1 : 0;
    logTargetUrlSpec.m_host.m_valuePos = logTargetUrlSpec.m_host.m_keyPos;
    std::string::size_type colonPos = hostPort.find(':');
    logTargetUrlSpec.m_host.m_value = hostPort.substr(0, colonPos);  // ok also if not found
    if (colonPos != std::string::npos) {
        std::string portStr = hostPort.substr(colonPos + 1);
        if (!parseIntProp("", "", portStr, logTargetUrlSpec.m_port.m_value, false)) {
            ELOG_REPORT_ERROR(
                "Invalid port specification in log target URL, expecting integer, seeing instead "
                "'%s' (context: %s)",
                portStr.c_str(), authority);
            return false;
        }
        logTargetUrlSpec.m_port.m_keyPos = basePos + atPos + colonPos + 1;
        logTargetUrlSpec.m_port.m_valuePos = logTargetUrlSpec.m_port.m_keyPos;
    }
    return true;
}

void ELogConfigParser::insertPropPosOverride(ELogPropertyPosMap& props, const std::string& key,
                                             const std::string& value, uint32_t keyPos,
                                             uint32_t valuePos) {
    std::pair<ELogPropertyPosMap::iterator, bool> itrRes =
        props.insert(ELogPropertyPosMap::value_type(key, {value.c_str(), keyPos, valuePos}));
    if (!itrRes.second) {
        itrRes.first->second.m_value = value;
        itrRes.first->second.m_keyPos = keyPos;
        itrRes.first->second.m_valuePos = valuePos;
    }
}

ELogConfigMapNode* ELogConfigParser::logTargetUrlToConfig(ELogTargetUrlSpec* urlSpec,
                                                          ELogConfigSourceContext* sourceContext) {
    // TODO: implement
    ELogConfigContext* context =
        new (std::nothrow) ELogConfigContext(sourceContext, urlSpec->m_scheme.m_keyPos, "");
    if (context == nullptr) {
        ELOG_REPORT_ERROR("Failed to allocate configuration context object, out of memory");
        return nullptr;
    }
    ELogConfigMapNode* mapNode = new (std::nothrow) ELogConfigMapNode(context);
    if (mapNode == nullptr) {
        ELOG_REPORT_ERROR("Failed to allocate configuration map node object, out of memory");
        delete context;
        return nullptr;
    }

    // add special fields first
    if (!urlSpec->m_user.m_value.empty()) {
        if (addConfigProperty(mapNode, "user", urlSpec->m_user)) {
            delete mapNode;
            return nullptr;
        }
    }
    if (!urlSpec->m_passwd.m_value.empty()) {
        if (addConfigProperty(mapNode, "password", urlSpec->m_passwd)) {
            delete mapNode;
            return nullptr;
        }
    }
    if (!urlSpec->m_host.m_value.empty()) {
        if (addConfigProperty(mapNode, "host", urlSpec->m_host)) {
            delete mapNode;
            return nullptr;
        }
    }
    if (urlSpec->m_port.m_value != 0) {
        if (addConfigIntProperty(mapNode, "port", urlSpec->m_port)) {
            delete mapNode;
            return nullptr;
        }
    }

    // now add all other properties
    for (const auto& entry : urlSpec->m_props) {
        const char* key = entry.first.c_str();
        const ELogPropertyPos& prop = entry.second;
        if (!addConfigProperty(mapNode, key, prop)) {
            delete mapNode;
            return nullptr;
        }
    }
    return mapNode;
}

bool ELogConfigParser::addConfigProperty(ELogConfigMapNode* mapNode, const char* key,
                                         const ELogPropertyPos& prop) {
    // TODO: key pos is not used
    ELogConfigContext* context = mapNode->makeConfigContext(prop.m_valuePos);
    if (context == nullptr) {
        ELOG_REPORT_ERROR("Failed to create configuration context object, out of memory");
        return false;
    }
    ELogConfigStringValue* value =
        new (std::nothrow) ELogConfigStringValue(context, prop.m_value.c_str());
    if (value == nullptr) {
        ELOG_REPORT_ERROR("Failed to allocate string configuration value, out of memory");
        delete context;
        return false;
    }
    if (!mapNode->addEntry(key, value)) {
        ELOG_REPORT_WARN("Failed to add '%s' property to configuration object, duplicate key", key);
        delete value;
        return false;
    }
    return true;
}

bool ELogConfigParser::addConfigIntProperty(ELogConfigMapNode* mapNode, const char* key,
                                            const ELogIntPropertyPos& prop) {
    // TODO: key pos is not used
    ELogConfigContext* context = mapNode->makeConfigContext(prop.m_valuePos);
    if (context == nullptr) {
        ELOG_REPORT_ERROR("Failed to create configuration context object, out of memory");
        return false;
    }
    ELogConfigIntValue* value = new (std::nothrow) ELogConfigIntValue(context, prop.m_value);
    if (value == nullptr) {
        ELOG_REPORT_ERROR("Failed to allocate integer configuration value, out of memory");
        delete context;
        return false;
    }
    if (!mapNode->addEntry(key, value)) {
        ELOG_REPORT_WARN("Failed to add '%s' property to configuration object, duplicate key", key);
        delete value;
        return false;
    }
    return true;
}

}  // namespace elog
