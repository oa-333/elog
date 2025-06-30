#include "elog_expression_parser.h"

#include <cstring>

#include "elog_error.h"
#include "elog_expression_tokenizer.h"

namespace elog {

static ELogExpression* parseExpression(ELogExpressionTokenizer& tok);
static ELogExpression* parseSubExpression(ELogExpressionTokenizer& tok);
static ELogExpression* parseAndExpression(ELogExpressionTokenizer& tok, ELogExpression* expr);
static ELogExpression* parseOrExpression(ELogExpressionTokenizer& tok, ELogExpression* expr);
static ELogExpression* parseCompositeExpression(ELogExpressionTokenizer& tok,
                                                ELogCompositeExpression* compExpr,
                                                ELogExprTokenType compTokenType,
                                                const char* compToken);
static ELogExpression* parseFunctionExpression(ELogExpressionTokenizer& tok, const char* lhs);
static ELogExpression* parsePrimitiveExpression(ELogExpressionTokenizer& tok, const char* lhs);

ELogExpression* ELogExpressionParser::parseExpressionString(const char* exprStr) {
    // we first dissect by parenthesis, where with each open parenthesis we recurse
    // until we face primitive expression: lhs OP rhs
    // when parsing LHS that is composite we may expect either AND, OR or end of expression
    // it is possible to encounter NOT before an expression, in which case the syntax becomes:
    // ((A) OR (B))  ====>>  ((NOT(A)) OR (B))
    // that is the NOT expression must be surrounded with parenthesis, and the expression it negates
    // also must be surrounded with parenthesis
    // the following operators are recognized:
    // ==, !=, <, <=, >, >=, LIKE, CONTAINS
    // the operands can be any string
    ELogExpressionTokenizer tok(exprStr);
    return parseExpression(tok);
}

ELogExpression* parseExpression(ELogExpressionTokenizer& tok) {
    ELogExprTokenType tokenType;
    std::string token;
    uint32_t tokenPos = 0;
    if (!tok.nextToken(tokenType, token, tokenPos)) {
        ELOG_REPORT_ERROR("Failed to parse expression, end of stream");
        return nullptr;
    }

    // an expression must be surrounded with parenthesis
    if (tokenType != ELogExprTokenType::TT_OPEN_PAREN) {
        ELOG_REPORT_ERROR("Invalid expression syntax, open parenthesis expected: %s",
                          tok.getErrLocStr(tokenPos).c_str());
        return nullptr;
    }

    // parse the sub-expression
    ELogExpression* expr = parseSubExpression(tok);
    if (expr == nullptr) {
        return nullptr;
    }

    // check next token
    if (!tok.nextToken(tokenType, token, tokenPos)) {
        ELOG_REPORT_ERROR("Failed to parse expression, end of stream");
        delete expr;
        return nullptr;
    }

    // simple expression is followed by close parenthesis
    if (tokenType == ELogExprTokenType::TT_CLOSE_PAREN) {
        return expr;
    }

    // now we may either see AND/OR operator
    if (tokenType == ELogExprTokenType::TT_AND) {
        return parseAndExpression(tok, expr);
    }
    if (tokenType == ELogExprTokenType::TT_OR) {
        return parseOrExpression(tok, expr);
    }

    ELOG_REPORT_ERROR("Invalid expression syntax, unexpected token: %s",
                      tok.getErrLocStr(tokenPos).c_str());
    delete expr;
    return nullptr;
}

ELogExpression* parseSubExpression(ELogExpressionTokenizer& tok) {
    ELogExprTokenType tokenType;
    std::string token;
    uint32_t tokenPos = 0;
    if (!tok.nextToken(tokenType, token, tokenPos)) {
        ELOG_REPORT_ERROR("Failed to parse expression, end of stream");
        return nullptr;
    }

    // sub-expression could start with an open parenthesis, in which case we parse it as a full
    // expression
    if (tokenType == ELogExprTokenType::TT_OPEN_PAREN) {
        tok.rewind(tokenPos);
        return parseExpression(tok);
    }

    // we could see here NOT
    if (tokenType == ELogExprTokenType::TT_NOT) {
        // parse the expression and wrap with NOT
        ELogExpression* expr = parseExpression(tok);
        if (expr == nullptr) {
            return nullptr;
        }
        ELogNotExpression* notExpr = new (std::nothrow) ELogNotExpression(expr);
        if (notExpr == nullptr) {
            ELOG_REPORT_ERROR("Failed to allocate NOT expression, out of memory");
            delete expr;
            return nullptr;
        }
        return notExpr;
    }

    // otherwise we have a primitive or function expression here
    if (tokenType != ELogExprTokenType::TT_TOKEN) {
        ELOG_REPORT_ERROR("Failed to parse expression, expecting string token for LHS operand: %s",
                          tok.getErrLocStr(tokenPos).c_str());
        return nullptr;
    }

    // we need to peek next token, if it is an open parenthesis, we have a function call expression
    if (tok.peekNextTokenType() == ELogExprTokenType::TT_OPEN_PAREN) {
        return parseFunctionExpression(tok, token.c_str());
    } else {
        return parsePrimitiveExpression(tok, token.c_str());
    }
}

ELogExpression* parseAndExpression(ELogExpressionTokenizer& tok, ELogExpression* expr) {
    ELogAndExpression* andExpr = new (std::nothrow) ELogAndExpression();
    if (andExpr == nullptr) {
        ELOG_REPORT_ERROR("Failed to allocate AND expression, out of memory");
        delete expr;
        return nullptr;
    }
    andExpr->m_expressions.push_back(expr);
    return parseCompositeExpression(tok, andExpr, ELogExprTokenType::TT_AND, "AND");
}

ELogExpression* parseOrExpression(ELogExpressionTokenizer& tok, ELogExpression* expr) {
    ELogAndExpression* andExpr = new (std::nothrow) ELogAndExpression();
    if (andExpr == nullptr) {
        ELOG_REPORT_ERROR("Failed to allocate AND expression, out of memory");
        delete expr;
        return nullptr;
    }
    andExpr->m_expressions.push_back(expr);
    return parseCompositeExpression(tok, andExpr, ELogExprTokenType::TT_OR, "OR");
}

ELogExpression* parseCompositeExpression(ELogExpressionTokenizer& tok,
                                         ELogCompositeExpression* compExpr,
                                         ELogExprTokenType compTokenType, const char* compToken) {
    // we expect an expression followed by AND/OR or close parenthesis
    ELogExprTokenType tokenType;
    std::string token;
    uint32_t tokenPos = 0;
    do {
        ELogExpression* expr = parseExpression(tok);
        if (expr == nullptr) {
            delete compExpr;
            return nullptr;
        }
        compExpr->m_expressions.push_back(expr);

        // now parse either operator or close parenthesis
        if (!tok.parseExpectedToken2(compTokenType, ELogExprTokenType::TT_CLOSE_PAREN, tokenType,
                                     token, tokenPos, compToken, ")")) {
            ELOG_REPORT_ERROR("Invalid expression syntax, expecting either '%s' or ')': %s",
                              compToken, tok.getErrLocStr(tokenPos).c_str());
            delete compExpr;
            return nullptr;
        }

        if (tokenType == ELogExprTokenType::TT_CLOSE_PAREN) {
            return compExpr;
        }
    } while (tok.hasMoreTokens());

    ELOG_REPORT_ERROR(
        "Invalid composite expression, premature end of stream, while expecting operand");
    delete compExpr;
    return nullptr;
}

ELogExpression* parseFunctionExpression(ELogExpressionTokenizer& tok, const char* functionName) {
    // first token is open parenthesis
    // then a comma-separates list of sub-expressions,
    // then a close parenthesis

    // we expect an expression followed by AND/OR or close parenthesis
    ELogExprTokenType tokenType;
    std::string token;
    uint32_t tokenPos = 0;
    if (!tok.nextToken(tokenType, token, tokenPos)) {
        ELOG_REPORT_ERROR("Internal error, unexpected end of stream after peeking next token");
        return nullptr;
    }
    if (tokenType != ELogExprTokenType::TT_OPEN_PAREN) {
        ELOG_REPORT_ERROR("Internal error, unexpected token after peeking next token");
        return nullptr;
    }

    // now we prepare a function expression
    // NOTE: we take care of special cases of AND/OR/NOT/CHAIN
    ELogCompositeExpression* funcExpr = nullptr;
    if (strcmp(functionName, "AND") == 0) {
        funcExpr = new (std::nothrow) ELogAndExpression();
    } else if (strcmp(functionName, "OR") == 0) {
        funcExpr = new (std::nothrow) ELogOrExpression();
    } else if (strcmp(functionName, "CHAIN") == 0) {
        funcExpr = new (std::nothrow) ELogChainExpression();
    } else {
        funcExpr = new (std::nothrow) ELogFunctionExpression(functionName);
    }
    if (funcExpr == nullptr) {
        ELOG_REPORT_ERROR("Failed to allocate function expression, out of memory");
        return nullptr;
    }

    // now we begin to parse sub-expressions
    do {
        ELogExpression* expr = parseSubExpression(tok);
        if (expr == nullptr) {
            delete funcExpr;
            return nullptr;
        }
        funcExpr->m_expressions.push_back(expr);

        // now parse either comma or close parenthesis
        if (!tok.parseExpectedToken2(ELogExprTokenType::TT_COMMA, ELogExprTokenType::TT_CLOSE_PAREN,
                                     tokenType, token, tokenPos, ",", ")")) {
            ELOG_REPORT_ERROR("Invalid function expression syntax, expecting either ',' or ')': %s",
                              tok.getErrLocStr(tokenPos).c_str());
            delete funcExpr;
            return nullptr;
        }

        if (tokenType == ELogExprTokenType::TT_CLOSE_PAREN) {
            return funcExpr;
        }
    } while (tok.hasMoreTokens());

    ELOG_REPORT_ERROR(
        "Invalid function expression, premature end of stream, while expecting operand");
    delete funcExpr;
    return nullptr;
}

ELogExpression* parsePrimitiveExpression(ELogExpressionTokenizer& tok, const char* lhs) {
    // parse operator
    ELogExprTokenType tokenType;
    std::string token;
    uint32_t tokenPos = 0;
    if (!tok.nextToken(tokenType, token, tokenPos)) {
        ELOG_REPORT_ERROR("Failed to parse expression, end of stream (expecting operator)");
        return nullptr;
    }
    if (tokenType == ELogExprTokenType::TT_CLOSE_PAREN ||
        tokenType == ELogExprTokenType::TT_COMMA) {
        ELogNameExpression* expr = new (std::nothrow) ELogNameExpression(lhs);
        if (expr == nullptr) {
            ELOG_REPORT_ERROR("Cannot allocate named expression, out of memory");
        }
        tok.rewind(tokenPos);
        return expr;
    }
    if (!tok.isOpToken(tokenType)) {
        ELOG_REPORT_ERROR("Failed to parse expression, expecting operator: %s",
                          tok.getErrLocStr(tokenPos).c_str());
        return nullptr;
    }
    std::string op = token;

    // parse RHS
    if (!tok.nextToken(tokenType, token, tokenPos)) {
        ELOG_REPORT_ERROR("Failed to parse expression, end of stream (expecting RHS operand)");
        return nullptr;
    }
    if (tokenType != ELogExprTokenType::TT_TOKEN) {
        ELOG_REPORT_ERROR("Failed to parse expression, expecting string token for RHS operand: %s",
                          tok.getErrLocStr(tokenPos).c_str());
        return nullptr;
    }
    std::string rhs = token;
    ELogOpExpression* expr = new (std::nothrow) ELogOpExpression(lhs, rhs.c_str(), op.c_str());
    if (expr == nullptr) {
        ELOG_REPORT_ERROR("Cannot allocate operation expression, out of memory");
    }
    return expr;
}

}  // namespace elog
