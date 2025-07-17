#include "elog_expression_tokenizer.h"

#include <cctype>
#include <cstring>

#include "elog_common.h"
#include "elog_error.h"

namespace elog {

static const char* sSpecialChars = "(){}[],=<>!:";

inline bool isSpecialChar(char c) { return strchr(sSpecialChars, c) != nullptr; }

ELogExpressionTokenizer::ELogExpressionTokenizer(const std::string& sourceStr)
    : m_sourceStr(trim(sourceStr)), m_pos(0) {}

bool ELogExpressionTokenizer::nextToken(ELogExprTokenType& tokenType, std::string& token,
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
    if (isSpecialChar(tokenChar)) {
        if (tokenChar == '(') {
            tokenType = ELogExprTokenType::TT_OPEN_PAREN;
            token = m_sourceStr.substr(tokenPos, 1);
        } else if (tokenChar == ')') {
            tokenType = ELogExprTokenType::TT_CLOSE_PAREN;
            token = m_sourceStr.substr(tokenPos, 1);
        } else if (tokenChar == ',') {
            tokenType = ELogExprTokenType::TT_COMMA;
            token = m_sourceStr.substr(tokenPos, 1);
        } else if (tokenChar == ':') {
            tokenType = ELogExprTokenType::TT_IS_OP;
            token = m_sourceStr.substr(tokenPos, 1);
        } else {
            // parse op
            // NOTE: there must be another char, otherwise expression syntax is bad
            if (m_pos == m_sourceStr.length()) {
                ELOG_REPORT_ERROR("Premature end of expression string, while parsing operator: %s",
                                  getErrLocStr(tokenPos).c_str());
                return false;
            }
            // now check if second char is also special
            if (isSpecialChar(m_sourceStr[m_pos])) {
                ++m_pos;
                token = m_sourceStr.substr(tokenPos, 2);
                if (token.compare("==") == 0) {
                    tokenType = ELogExprTokenType::TT_EQ_OP;
                } else if (token.compare("!=") == 0) {
                    tokenType = ELogExprTokenType::TT_NEQ_OP;
                } else if (token.compare("<=") == 0) {
                    tokenType = ELogExprTokenType::TT_LE_OP;
                } else if (token.compare(">=") == 0) {
                    tokenType = ELogExprTokenType::TT_GE_OP;
                } else {
                    ELOG_REPORT_ERROR("Invalid operator token '%s': %s", token.c_str(),
                                      getErrLocStr(tokenPos).c_str());
                    return false;
                }
            } else {
                // single char operator, can only be < or >
                token = m_sourceStr.substr(tokenPos, 1);
                if (token.compare("<") == 0) {
                    tokenType = ELogExprTokenType::TT_LT_OP;
                } else if (token.compare(">") == 0) {
                    tokenType = ELogExprTokenType::TT_GT_OP;
                } else {
                    ELOG_REPORT_ERROR("Invalid operator token '%s': %s", token.c_str(),
                                      getErrLocStr(tokenPos).c_str());
                    return false;
                }
            }
        }
    } else {
        // text token, parse until special char or white space, or end of stream
        while (m_pos < m_sourceStr.length() && !std::isspace(m_sourceStr[m_pos]) &&
               !isSpecialChar(m_sourceStr[m_pos])) {
            ++m_pos;
        }
        token = m_sourceStr.substr(tokenPos, m_pos - tokenPos);
        if (token.compare("AND") == 0 || token.compare("and") == 0) {
            tokenType = ELogExprTokenType::TT_AND;
        } else if (token.compare("OR") == 0 || token.compare("or") == 0) {
            tokenType = ELogExprTokenType::TT_OR;
        } else if (token.compare("NOT") == 0 || token.compare("not") == 0) {
            tokenType = ELogExprTokenType::TT_NOT;
        } else if (token.compare("LIKE") == 0 || token.compare("like") == 0) {
            tokenType = ELogExprTokenType::TT_LIKE_OP;
        } else if (token.compare("CONTAINS") == 0 || token.compare("contains") == 0) {
            tokenType = ELogExprTokenType::TT_CONTAINS_OP;
        } else {
            tokenType = ELogExprTokenType::TT_TOKEN;
        }
    }

    return true;
}

ELogExprTokenType ELogExpressionTokenizer::peekNextTokenType() {
    ELogExprTokenType tokenType = ELogExprTokenType::TT_INVALID;
    std::string token;
    uint32_t tokenPos = 0;
    if (nextToken(tokenType, token, tokenPos)) {
        rewind(tokenPos);
    }
    return tokenType;
}

bool ELogExpressionTokenizer::isOpToken(ELogExprTokenType tokenType) {
    return (tokenType == ELogExprTokenType::TT_EQ_OP || tokenType == ELogExprTokenType::TT_NEQ_OP ||
            tokenType == ELogExprTokenType::TT_LT_OP || tokenType == ELogExprTokenType::TT_LE_OP ||
            tokenType == ELogExprTokenType::TT_GT_OP || tokenType == ELogExprTokenType::TT_GE_OP ||
            tokenType == ELogExprTokenType::TT_LIKE_OP ||
            tokenType == ELogExprTokenType::TT_CONTAINS_OP ||
            tokenType == ELogExprTokenType::TT_IS_OP);
}

std::string ELogExpressionTokenizer::getErrLocStr(uint32_t tokenPos) const {
    return m_sourceStr.substr(0, tokenPos) + RED " | HERE ===>>> | " RESET +
           m_sourceStr.substr(tokenPos);
}

bool ELogExpressionTokenizer::parseExpectedToken(ELogExprTokenType expectedTokenType,
                                                 std::string& token, const char* expectedStr) {
    uint32_t pos = 0;
    ELogExprTokenType tokenType;
    if (!hasMoreTokens() || !nextToken(tokenType, token, pos)) {
        ELOG_REPORT_ERROR("Unexpected enf of expression specification");
        return false;
    }
    if (tokenType != expectedTokenType) {
        ELOG_REPORT_ERROR("Invalid token in expression specification, expected %s, at pos %u: %s",
                          expectedStr, pos, getSourceStr());
        ELOG_REPORT_ERROR("Error location: %s", getErrLocStr(pos).c_str());
        return false;
    }
    return true;
}

bool ELogExpressionTokenizer::parseExpectedToken2(ELogExprTokenType expectedTokenType1,
                                                  ELogExprTokenType expectedTokenType2,
                                                  ELogExprTokenType& tokenType, std::string& token,
                                                  uint32_t& tokenPos, const char* expectedStr1,
                                                  const char* expectedStr2) {
    if (!hasMoreTokens() || !nextToken(tokenType, token, tokenPos)) {
        ELOG_REPORT_ERROR("Unexpected enf of expression specification");
        return false;
    }
    if (tokenType != expectedTokenType1 && tokenType != expectedTokenType2) {
        ELOG_REPORT_ERROR(
            "Invalid token in expression specification, expected either %s or %s, at pos "
            "%u: %s",
            expectedStr1, expectedStr2, tokenPos, getSourceStr());
        ELOG_REPORT_ERROR("Error location: %s", getErrLocStr(tokenPos).c_str());
        return false;
    }
    return true;
}

bool ELogExpressionTokenizer::parseExpectedToken3(ELogExprTokenType expectedTokenType1,
                                                  ELogExprTokenType expectedTokenType2,
                                                  ELogExprTokenType expectedTokenType3,
                                                  ELogExprTokenType& tokenType, std::string& token,
                                                  uint32_t& tokenPos, const char* expectedStr1,
                                                  const char* expectedStr2,
                                                  const char* expectedStr3) {
    if (!hasMoreTokens() || !nextToken(tokenType, token, tokenPos)) {
        ELOG_REPORT_ERROR("Unexpected enf of expression specification");
        return false;
    }
    if (tokenType != expectedTokenType1 && tokenType != expectedTokenType2 &&
        tokenType != expectedTokenType3) {
        ELOG_REPORT_ERROR(
            "Invalid token in expression specification, expected either %s, %s, or %s, at "
            "pos %u: %s",
            expectedStr1, expectedStr2, expectedStr3, tokenPos, getSourceStr());
        ELOG_REPORT_ERROR("Error location: %s", getErrLocStr(tokenPos).c_str());
        return false;
    }
    return true;
}

}  // namespace elog
