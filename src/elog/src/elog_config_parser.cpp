#include "elog_config_parser.h"

#include <algorithm>
#include <cstring>

#include "elog.h"
#include "elog_common.h"
#include "elog_report.h"
#include "elog_schema_manager.h"

namespace elog {

bool ELogConfigParser::parseLogLevel(const char* logLevelStr, ELogLevel& logLevel,
                                     ELogPropagateMode& propagateMode) {
    const char* ptr = nullptr;
    size_t parseLen = 0;
    if (!elogLevelFromStr(logLevelStr, logLevel, &ptr, &parseLen)) {
        ELOG_REPORT_ERROR("Invalid log level: %s", logLevelStr);
        return false;
    }

    // parse optional propagation sign if there is any
    propagateMode = ELogPropagateMode::PM_NONE;
    size_t len = strlen(logLevelStr);
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
                                  tokenizer.getErrLocStr(tokenPos).c_str());
            } else {
                ELOG_REPORT_ERROR("Duplicate comma in log target list: %s",
                                  tokenizer.getErrLocStr(tokenPos).c_str());
            }
            return false;
        }
        ELogTargetId logTargetId = elog::getLogTargetId(token.c_str());
        if (logTargetId == ELOG_INVALID_TARGET_ID) {
            ELOG_REPORT_ERROR("Invalid log target list, unknown log target '%s", token.c_str());
            return false;
        }
        ELOG_ADD_TARGET_AFFINITY_MASK(mask, logTargetId);
    }
    return true;
}

ELogConfig* ELogConfigParser::parseLogTargetConfig(const std::string& logTargetUrl) {
    // the configuration string may be give as a URL or in nested form
    // we distinguish between the cases by the appearance of enclosing curly braces
    std::string trimmedUrl = trim(logTargetUrl);
    if (trimmedUrl.starts_with("{") && trimmedUrl.ends_with("}")) {
        ELogConfig* config = ELogConfig::loadFromString(trimmedUrl.c_str());
        if (config != nullptr) {
            return config;
        }
    }

    // if we failed, we default to URL parsing

    // first parse the url, then convert to configuration object
    // NOTE: if asynchronous log target specification is embedded, it is now done through the
    // fragment part of the URL (which can theoretically be repeated)
    ELogTargetUrlSpec urlSpec;
    if (!parseLogTargetUrl(logTargetUrl, urlSpec)) {
        return nullptr;
    }

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
            ELOG_REPORT_ERROR("Failed to add entry log_target to configuration map");
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

void ELogConfigParser::insertPropOverride(ELogPropertyMap& props, const std::string& key,
                                          const std::string& value) {
    std::pair<ELogPropertyMap::iterator, bool> itrRes =
        props.insert(ELogPropertyMap::value_type(key, value));
    if (!itrRes.second) {
        itrRes.first->second = value;
    }
}

bool ELogConfigParser::parseLogTargetUrl(const std::string& logTargetUrl,
                                         ELogTargetUrlSpec& logTargetUrlSpec,
                                         size_t basePos /* = 0 */) {
    // first parse the url, then convert to configuration object
    // since we may need to specify a sub-target we use the fragment part to specify a sub-target
    std::string::size_type hashPos = logTargetUrl.find('|');
    if (hashPos != std::string::npos) {
        std::string parentUrl = trim(logTargetUrl.substr(0, hashPos));
        if (!parseLogTargetUrl(parentUrl, logTargetUrlSpec, basePos)) {
            ELOG_REPORT_ERROR("Failed to parse top-level log target RUL specification");
            return false;
        }
        logTargetUrlSpec.m_subUrlSpec = new (std::nothrow) ELogTargetUrlSpec();
        if (logTargetUrlSpec.m_subUrlSpec == nullptr) {
            ELOG_REPORT_ERROR("Failed to allocate log target URL object, out of memory");
            return false;
        }
        std::string childUrl = trim(logTargetUrl.substr(hashPos + 1));
        if (!parseLogTargetUrl(childUrl, *logTargetUrlSpec.m_subUrlSpec, basePos + hashPos + 1)) {
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
    logTargetUrlSpec.m_scheme.m_keyPos = basePos;
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
        size_t keyPos = basePos + prevPos;

        // parse to key=value and add to props map (could be there is no value specified)
        std::string::size_type equalPos = prop.find('=');
        if (equalPos != std::string::npos) {
            std::string key = trim(prop.substr(0, equalPos));
            std::string value = trim(prop.substr(equalPos + 1));
            size_t valuePos = keyPos + equalPos + 1;
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
            if (!insertPropPosOverride(logTargetUrlSpec.m_props, key, value, keyPos, valuePos)) {
                ELOG_REPORT_ERROR("Failed to insert entry to property map");
                return false;
            }
        } else {
            if (!insertPropPosOverride(logTargetUrlSpec.m_props, trim(prop), "", keyPos, 0)) {
                ELOG_REPORT_ERROR("Failed to insert generic entry to property map");
                return false;
            }
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

bool ELogConfigParser::parseUrlPath(ELogTargetUrlSpec& logTargetUrlSpec, size_t basePos) {
    // the path may be specified as follows: authority/path
    // the authority may be specified as follows: [userinfo "@"] host [":" port]
    // and user info is: [user[:password]
    // so we first search for a slash, and anything preceding it is the authority part
    std::string::size_type slashPos = logTargetUrlSpec.m_path.m_value.find('/');
    // authority part is optional, so we may not find a slash at all
    if (slashPos == std::string::npos) {
        return true;
    }

    // it may also happen that the path contains a slash (and no authority is specified), in which
    // case we will see 3 slashes, otherwise the first part of the path can be mistakenly parsed as
    // the authority part.
    // for example: file://./log_dir/app.log
    // in this case, the dot will be parsed mistakenly as the authority part (yielding host "."),
    // rather than being part of the relative file path. so in order to avoid this error, the
    // correct way is to add a third slash as follows:
    // file:///./log_dir/app.log
    // this way the authority part becomes the empty string, and the path is "./log_dir/app.log"
    if (slashPos == 0) {
        // when a third slash appears, it is found at position zero after the schema marker "://",
        // but the path at this parsing phase still contains the third slash, so we need to remove
        // it, and there is no need to parse the authority part
        logTargetUrlSpec.m_path.m_value = logTargetUrlSpec.m_path.m_value.substr(1);
        ++logTargetUrlSpec.m_path.m_keyPos;
        ++logTargetUrlSpec.m_path.m_valuePos;
        return true;
    }
    // TODO: add to README - if the path contains slash characters, be sure to add a third slash
    // after the schema marker to avoid having the first part of the path being mistakenly
    // interpreted as the authority part of the URL (i.e. user/password, host/port specification).

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
                portStr.c_str(), authority.c_str());
            return false;
        }
        logTargetUrlSpec.m_port.m_keyPos = basePos + atPos + colonPos + 1;
        logTargetUrlSpec.m_port.m_valuePos = logTargetUrlSpec.m_port.m_keyPos;
    }
    return true;
}

bool ELogConfigParser::insertPropPosOverride(ELogPropertyPosMap& props, const std::string& key,
                                             const std::string& value, size_t keyPos,
                                             size_t valuePos) {
    // we need to figure out the value type (bool, int or string)
    ELogPropertyPos* prop = nullptr;
    if (value.compare("true") == 0 || value.compare("yes") == 0 || value.compare("on") == 0) {
        prop = new (std::nothrow) ELogBoolPropertyPos(true, keyPos, valuePos);
    } else if (value.compare("false") == 0 || value.compare("no") == 0 ||
               value.compare("off") == 0) {
        prop = new (std::nothrow) ELogBoolPropertyPos(false, keyPos, valuePos);
    } else {
        // try to parse as integer
        int64_t intValue = 0;
        if (parseIntProp("", "", value, intValue, false)) {
            prop = new (std::nothrow) ELogIntPropertyPos(intValue, keyPos, valuePos);
        } else {
            prop = new (std::nothrow) ELogStringPropertyPos(value.c_str(), keyPos, valuePos);
        }
    }
    if (prop == nullptr) {
        ELOG_REPORT_ERROR("Failed to allocate configuration property, out of memory");
        return false;
    }

    std::pair<ELogPropertyPosMap::MapType::iterator, bool> itrRes =
        props.m_map.insert(ELogPropertyPosMap::MapType::value_type(key, prop));
    if (!itrRes.second) {
        ELogPropertyPos* existingProp = itrRes.first->second;
        if (existingProp->m_type != prop->m_type) {
            ELOG_REPORT_ERROR("Mismatching property types, cannot override");
            delete prop;
            return false;
        }
        if (existingProp->m_type == ELogPropertyType::PT_STRING) {
            ((ELogStringPropertyPos*)existingProp)->m_value =
                ((ELogStringPropertyPos*)prop)->m_value;
        } else if (existingProp->m_type == ELogPropertyType::PT_INT) {
            ((ELogIntPropertyPos*)existingProp)->m_value = ((ELogIntPropertyPos*)prop)->m_value;
        } else if (existingProp->m_type == ELogPropertyType::PT_BOOL) {
            ((ELogBoolPropertyPos*)existingProp)->m_value = ((ELogBoolPropertyPos*)prop)->m_value;
        } else {
            ELOG_REPORT_ERROR("Invalid property type, data corrupt");
            delete prop;
            return false;
        }
        existingProp->m_keyPos = prop->m_keyPos;
        existingProp->m_valuePos = prop->m_valuePos;
        delete prop;
    }
    return true;
}

ELogConfigMapNode* ELogConfigParser::logTargetUrlToConfig(ELogTargetUrlSpec* urlSpec,
                                                          ELogConfigSourceContext* sourceContext) {
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
    if (!addConfigProperty(mapNode, "scheme", &urlSpec->m_scheme)) {
        delete mapNode;
        return nullptr;
    }
    if (!addConfigProperty(mapNode, "path", &urlSpec->m_path)) {
        delete mapNode;
        return nullptr;
    }
    // many schema handlers use 'type' as the key and not path, so we add this synonym
    if (!addConfigProperty(mapNode, "type", &urlSpec->m_path)) {
        delete mapNode;
        return nullptr;
    }
    if (!urlSpec->m_user.m_value.empty()) {
        if (!addConfigProperty(mapNode, "user", &urlSpec->m_user)) {
            delete mapNode;
            return nullptr;
        }
    }
    if (!urlSpec->m_passwd.m_value.empty()) {
        if (!addConfigProperty(mapNode, "password", &urlSpec->m_passwd)) {
            delete mapNode;
            return nullptr;
        }
    }
    if (!urlSpec->m_host.m_value.empty()) {
        if (!addConfigProperty(mapNode, "host", &urlSpec->m_host)) {
            delete mapNode;
            return nullptr;
        }
    }
    if (urlSpec->m_port.m_value != 0) {
        if (!addConfigProperty(mapNode, "port", &urlSpec->m_port)) {
            delete mapNode;
            return nullptr;
        }
    }

    // now add all other properties
    for (const auto& entry : urlSpec->m_props.m_map) {
        const char* key = entry.first.c_str();
        const ELogPropertyPos* prop = entry.second;
        if (!addConfigProperty(mapNode, key, prop)) {
            delete mapNode;
            return nullptr;
        }
    }
    return mapNode;
}

bool ELogConfigParser::addConfigProperty(ELogConfigMapNode* mapNode, const char* key,
                                         const ELogPropertyPos* prop) {
    // TODO: key pos is not used
    ELogConfigContext* context = mapNode->makeConfigContext(prop->m_valuePos);
    if (context == nullptr) {
        ELOG_REPORT_ERROR("Failed to create configuration context object, out of memory");
        return false;
    }
    ELogConfigValue* value = ELogConfig::loadValueFromProp(context, key, prop);
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

}  // namespace elog
