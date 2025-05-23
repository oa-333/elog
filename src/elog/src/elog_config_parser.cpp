#include "elog_config_parser.h"

#include <algorithm>
#include <cstring>

#include "elog_common.h"
#include "elog_error.h"
#include "elog_schema_manager.h"

namespace elog {

bool ELogConfigParser::parseLogLevel(const char* logLevelStr, ELogLevel& logLevel,
                                     ELogSource::PropagateMode& propagateMode) {
    const char* ptr = nullptr;
    if (!elogLevelFromStr(logLevelStr, logLevel, &ptr)) {
        ELOG_REPORT_ERROR("Invalid log level: %s", logLevelStr);
        return false;
    }

    // parse optional propagation sign if there is any
    propagateMode = ELogSource::PropagateMode::PM_NONE;
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
            propagateMode = ELogSource::PropagateMode::PM_SET;
        } else if (*ptr == '-') {
            propagateMode = ELogSource::PropagateMode::PM_RESTRICT;
        } else if (*ptr == '+') {
            propagateMode = ELogSource::PropagateMode::PM_LOOSE;
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

bool ELogConfigParser::parseLogTargetSpec(const std::string& logTargetCfg,
                                          ELogTargetNestedSpec& logTargetNestedSpec,
                                          ELogTargetSpecStyle& specStyle) {
    specStyle = ELOG_STYLE_URL;
    if (logTargetCfg.starts_with("{") || logTargetCfg.starts_with("[")) {
        if (!parseLogTargetNestedSpec(logTargetCfg, logTargetNestedSpec)) {
            ELOG_REPORT_ERROR("Invalid log target specification: %s", logTargetCfg.c_str());
            return false;
        }
        specStyle = ELOG_STYLE_NESTED;
    } else if (!parseLogTargetSpec(logTargetCfg, logTargetNestedSpec.m_spec)) {
        ELOG_REPORT_ERROR("Invalid log target specification: %s", logTargetCfg.c_str());
        return false;
    }
    return true;
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
    ELogSpecTokenizer tok(logTargetCfg);
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

static bool parseExpectedToken(ELogSpecTokenizer& tok, ELogTokenType expectedTokenType,
                               std::string& token, const char* expectedStr) {
    uint32_t pos = 0;
    ELogTokenType tokenType;
    if (!tok.hasMoreTokens() || !tok.nextToken(tokenType, token, pos)) {
        ELOG_REPORT_ERROR("Unexpected enf of log target nested specification");
        return false;
    }
    if (tokenType != expectedTokenType) {
        ELOG_REPORT_ERROR(
            "Invalid token in nested log target specification, expected %s, at pos %u: %s",
            expectedStr, pos, tok.getSpec());
        ELOG_REPORT_ERROR("Error location: %s", tok.getErrLocStr(pos).c_str());
        return false;
    }
    return true;
}

static bool parseExpectedToken2(ELogSpecTokenizer& tok, ELogTokenType expectedTokenType1,
                                ELogTokenType expectedTokenType2, ELogTokenType& tokenType,
                                std::string& token, uint32_t& tokenPos, const char* expectedStr1,
                                const char* expectedStr2) {
    if (!tok.hasMoreTokens() || !tok.nextToken(tokenType, token, tokenPos)) {
        ELOG_REPORT_ERROR("Unexpected enf of log target nested specification");
        return false;
    }
    if (tokenType != expectedTokenType1 && tokenType != expectedTokenType2) {
        ELOG_REPORT_ERROR(
            "Invalid token in nested log target specification, expected either %s or %s, at pos "
            "%u: %s",
            expectedStr1, expectedStr2, tokenPos, tok.getSpec());
        ELOG_REPORT_ERROR("Error location: %s", tok.getErrLocStr(tokenPos).c_str());
        return false;
    }
    return true;
}

static bool parseExpectedToken3(ELogSpecTokenizer& tok, ELogTokenType expectedTokenType1,
                                ELogTokenType expectedTokenType2, ELogTokenType expectedTokenType3,
                                ELogTokenType& tokenType, std::string& token, uint32_t& tokenPos,
                                const char* expectedStr1, const char* expectedStr2,
                                const char* expectedStr3) {
    if (!tok.hasMoreTokens() || !tok.nextToken(tokenType, token, tokenPos)) {
        ELOG_REPORT_ERROR("Unexpected enf of log target nested specification");
        return false;
    }
    if (tokenType != expectedTokenType1 && tokenType != expectedTokenType2 &&
        tokenType != expectedTokenType3) {
        ELOG_REPORT_ERROR(
            "Invalid token in nested log target specification, expected either %s, %s, or %s, at "
            "pos %u: %s",
            expectedStr1, expectedStr2, expectedStr3, tokenPos, tok.getSpec());
        ELOG_REPORT_ERROR("Error location: %s", tok.getErrLocStr(tokenPos).c_str());
        return false;
    }
    return true;
}

bool ELogConfigParser::parseLogTargetNestedSpec(const std::string& logTargetCfg,
                                                ELogTargetNestedSpec& logTargetNestedSpec,
                                                ELogSpecTokenizer& tok) {
    std::string token;

    // first token must be open brace
    if (!parseExpectedToken(tok, ELogTokenType::TT_OPEN_BRACE, token, "'{'")) {
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
            if (!parseExpectedToken2(tok, ELogTokenType::TT_TOKEN, ELogTokenType::TT_CLOSE_BRACE,
                                     tokenType, key, tokenPos, "text", "'}'")) {
                return false;
            }
            // move to next state
            parseState = (tokenType == ELogTokenType::TT_CLOSE_BRACE) ? PS_DONE : PS_KEY;
        }

        // parse state: KEY
        else if (parseState == PS_KEY) {
            // expecting equal sign
            if (!parseExpectedToken(tok, ELogTokenType::TT_EQUAL_SIGN, token, "'='")) {
                return false;
            }
            // move to state PS_EQUAL
            parseState = PS_EQUAL;
        }

        // parse state: EQUAL
        else if (parseState == PS_EQUAL) {
            // expecting either value, open brace or open brackets
            if (!parseExpectedToken3(tok, ELogTokenType::TT_TOKEN, ELogTokenType::TT_OPEN_BRACE,
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
            if (!parseExpectedToken2(tok, ELogTokenType::TT_COMMA, ELogTokenType::TT_CLOSE_BRACKET,
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
            if (!parseExpectedToken2(tok, ELogTokenType::TT_COMMA, ELogTokenType::TT_CLOSE_BRACE,
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

}  // namespace elog
