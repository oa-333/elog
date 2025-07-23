#include "elog_string_tokenizer.h"

#include <cctype>
#include <cstring>

#include "elog_common.h"
#include "elog_report.h"

namespace elog {

static const char* sSpecialChars = "{}[],=";

inline bool isSpecialChar(char c) { return strchr(sSpecialChars, c) != nullptr; }

ELogStringTokenizer::ELogStringTokenizer(const std::string& sourceStr)
    : m_sourceStr(trim(sourceStr)), m_pos(0) {}

bool ELogStringTokenizer::nextToken(ELogTokenType& tokenType, std::string& token,
                                    uint32_t& tokenPos) {
    // waste white space
    while (m_pos < m_sourceStr.length() && std::isspace(m_sourceStr[m_pos])) {
        ++m_pos;
    }

    if (m_pos == m_sourceStr.length()) {
        return false;
    }

    // NOTE: must advance beyond single char token, otherwise we get stuck in same pos
    tokenPos = m_pos++;
    char tokenChar = m_sourceStr[tokenPos];
    if (tokenChar == '{') {
        tokenType = ELogTokenType::TT_OPEN_BRACE;
    } else if (tokenChar == '}') {
        tokenType = ELogTokenType::TT_CLOSE_BRACE;
    } else if (tokenChar == '[') {
        tokenType = ELogTokenType::TT_OPEN_BRACKET;
    } else if (tokenChar == ']') {
        tokenType = ELogTokenType::TT_CLOSE_BRACKET;
    } else if (tokenChar == ',') {
        tokenType = ELogTokenType::TT_COMMA;
    } else if (tokenChar == '=') {
        tokenType = ELogTokenType::TT_EQUAL_SIGN;
    } else if (tokenChar == ':') {
        tokenType = ELogTokenType::TT_COLON_SIGN;
    } else if (tokenChar == '"') {
        // quote token, we parse until next quote is found
        while (m_pos < m_sourceStr.length() && m_sourceStr[m_pos] != '"') {
            ++m_pos;
        }
        if (m_pos == m_sourceStr.length()) {
            ELOG_REPORT_ERROR("Missing terminating quote while tokening string");
            return false;
        }
        ++m_pos;
        token = m_sourceStr.substr(tokenPos, m_pos - tokenPos);
        tokenType = ELogTokenType::TT_TOKEN;
    } else if (tokenChar == '\'') {
        // quote token, we parse until next quote is found
        while (m_pos < m_sourceStr.length() && m_sourceStr[m_pos] != '\'') {
            ++m_pos;
        }
        if (m_pos == m_sourceStr.length()) {
            ELOG_REPORT_ERROR("Missing terminating quote while tokening string");
            return false;
        }
        ++m_pos;
        token = m_sourceStr.substr(tokenPos, m_pos - tokenPos);
        tokenType = ELogTokenType::TT_TOKEN;
    } else {
        // text token, parse until special char or white space, or end of stream
        while (m_pos < m_sourceStr.length() && !std::isspace(m_sourceStr[m_pos]) &&
               !isSpecialChar(m_sourceStr[m_pos])) {
            ++m_pos;
        }
        token = m_sourceStr.substr(tokenPos, m_pos - tokenPos);
        tokenType = ELogTokenType::TT_TOKEN;
    }

    return true;
}

ELogTokenType ELogStringTokenizer::peekNextTokenType() {
    ELogTokenType tokenType = ELogTokenType::TT_INVALID;
    std::string token;
    uint32_t tokenPos = 0;
    if (nextToken(tokenType, token, tokenPos)) {
        rewind(tokenPos);
    }
    return tokenType;
}

std::string ELogStringTokenizer::getErrLocStr(uint32_t tokenPos) const {
    return m_sourceStr.substr(0, tokenPos) + RED " | HERE ===>>> | " RESET +
           m_sourceStr.substr(tokenPos);
}

bool ELogStringTokenizer::parseExpectedToken(ELogTokenType expectedTokenType, std::string& token,
                                             const char* expectedStr) {
    uint32_t pos = 0;
    ELogTokenType tokenType;
    if (!hasMoreTokens() || !nextToken(tokenType, token, pos)) {
        ELOG_REPORT_ERROR("Unexpected enf of log target nested specification");
        return false;
    }
    if (tokenType != expectedTokenType) {
        ELOG_REPORT_ERROR(
            "Invalid token in nested log target specification, expected %s, at pos %u: %s",
            expectedStr, pos, getSourceStr());
        ELOG_REPORT_ERROR("Error location: %s", getErrLocStr(pos).c_str());
        return false;
    }
    return true;
}

bool ELogStringTokenizer::parseExpectedToken2(ELogTokenType expectedTokenType1,
                                              ELogTokenType expectedTokenType2,
                                              ELogTokenType& tokenType, std::string& token,
                                              uint32_t& tokenPos, const char* expectedStr1,
                                              const char* expectedStr2) {
    if (!hasMoreTokens() || !nextToken(tokenType, token, tokenPos)) {
        ELOG_REPORT_ERROR("Unexpected enf of log target nested specification");
        return false;
    }
    if (tokenType != expectedTokenType1 && tokenType != expectedTokenType2) {
        ELOG_REPORT_ERROR(
            "Invalid token in nested log target specification, expected either %s or %s, at pos "
            "%u: %s",
            expectedStr1, expectedStr2, tokenPos, getSourceStr());
        ELOG_REPORT_ERROR("Error location: %s", getErrLocStr(tokenPos).c_str());
        return false;
    }
    return true;
}

bool ELogStringTokenizer::parseExpectedToken3(ELogTokenType expectedTokenType1,
                                              ELogTokenType expectedTokenType2,
                                              ELogTokenType expectedTokenType3,
                                              ELogTokenType& tokenType, std::string& token,
                                              uint32_t& tokenPos, const char* expectedStr1,
                                              const char* expectedStr2, const char* expectedStr3) {
    if (!hasMoreTokens() || !nextToken(tokenType, token, tokenPos)) {
        ELOG_REPORT_ERROR("Unexpected enf of log target nested specification");
        return false;
    }
    if (tokenType != expectedTokenType1 && tokenType != expectedTokenType2 &&
        tokenType != expectedTokenType3) {
        ELOG_REPORT_ERROR(
            "Invalid token in nested log target specification, expected either %s, %s, or %s, at "
            "pos %u: %s",
            expectedStr1, expectedStr2, expectedStr3, tokenPos, getSourceStr());
        ELOG_REPORT_ERROR("Error location: %s", getErrLocStr(tokenPos).c_str());
        return false;
    }
    return true;
}

}  // namespace elog
