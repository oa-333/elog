#ifndef __ELOG_EXPRESSION_H__
#define __ELOG_EXPRESSION_H__

#include <cstdint>
#include <string>
#include <vector>

namespace elog {

/** @enum Expression type constants. */
enum class ELogExpressionType : uint32_t {
    /** @brief AND expression type. */
    ET_AND_EXPR,

    /** @brief OR expression type. */
    ET_OR_EXPR,

    /** @brief Not expression type. */
    ET_NOT_EXPR,

    /** @brief Chain expression type. */
    ET_CHAIN_EXPR,

    /** @brief Function expression type. */
    ET_FUNC_EXPR,

    /** @brief Operation expression type. */
    ET_OP_EXPR,

    /** @brief Name-only expression type. */
    ET_NAME_EXPR
};

// in order to support group flush, we need to support function-like call:
//
//      group(size == X, timeout == Y),
//
// and so we add a func expression with named args, but this syntax will cause sub-expressions to be
// interpreted as size filter and timed filter, so we introduce a new syntax with colon character,
// denoting named value, rather than predicate, like this:
//
//      group(group_size:4, group_timeout_micros:100)
//
// Now, for group flush policy this is not enough, since we need to specify both when flush should
// take place (the controlling policy) and how (the moderating policy), so for this purpose the
// CHAIN syntax is introduced. The CHAIN keyword denotes tying two policies together, the first
// being the controlling policy and the second being the moderating policy:
//
//      flush_policy=(CHAIN(immediate, group(group_size:4, group_timeout_micros:100)))
//
// CHAIN syntax for group flush could have been done just like AND/OR as follows:
//
// ((immediate) CHAIN (group(size:5, timeout:100ms)))
//
// The choice has been made to support function call style, which is more intuitive for a predicate
// expression. So full function syntax is supported like this:
//
//      <function-name>(<comma-separated predicate/expression list)
//
// this syntax supports all composite expression as a function:
//
// AND(expr1, expr2, ...)
// OR(expr1, expr2, ...)
// CHAIN((expr1, expr2)
// group(size == 5, timeout == 100ms)
//

struct ELogExpression {
    ELogExpressionType m_type;

    ELogExpression(ELogExpressionType type) : m_type(type) {}
    virtual ~ELogExpression() {}
};

struct ELogCompositeExpression : public ELogExpression {
    std::vector<ELogExpression*> m_expressions;
    ELogCompositeExpression(ELogExpressionType type) : ELogExpression(type) {}
    ~ELogCompositeExpression() override {
        for (ELogExpression* expr : m_expressions) {
            delete expr;
        }
        m_expressions.clear();
    }
};

struct ELogAndExpression : public ELogCompositeExpression {
    ELogAndExpression() : ELogCompositeExpression(ELogExpressionType::ET_AND_EXPR) {}
    ~ELogAndExpression() final {}
};

struct ELogOrExpression : public ELogCompositeExpression {
    ELogOrExpression() : ELogCompositeExpression(ELogExpressionType::ET_OR_EXPR) {}
    ~ELogOrExpression() final {}
};

struct ELogChainExpression : public ELogCompositeExpression {
    ELogChainExpression() : ELogCompositeExpression(ELogExpressionType::ET_CHAIN_EXPR) {}
    ~ELogChainExpression() final {}
};

struct ELogFunctionExpression : public ELogCompositeExpression {
    std::string m_functionName;
    ELogFunctionExpression(const char* functionName)
        : ELogCompositeExpression(ELogExpressionType::ET_FUNC_EXPR), m_functionName(functionName) {}
    ~ELogFunctionExpression() final {}
};

struct ELogNotExpression : public ELogExpression {
    ELogExpression* m_expression;
    ELogNotExpression(ELogExpression* expr = nullptr)
        : ELogExpression(ELogExpressionType::ET_NOT_EXPR), m_expression(expr) {}
    ~ELogNotExpression() final {
        if (m_expression != nullptr) {
            delete m_expression;
            m_expression = nullptr;
        }
    }
};

struct ELogOpExpression : public ELogExpression {
    std::string m_lhs;
    std::string m_rhs;
    std::string m_op;

    ELogOpExpression(const char* lhs = "", const char* rhs = "", const char* op = "")
        : ELogExpression(ELogExpressionType::ET_OP_EXPR), m_lhs(lhs), m_rhs(rhs), m_op(op) {}
    ~ELogOpExpression() final {}
};

struct ELogNameExpression : public ELogExpression {
    std::string m_name;

    ELogNameExpression(const char* name = "")
        : ELogExpression(ELogExpressionType::ET_NAME_EXPR), m_name(name) {}
    ~ELogNameExpression() final {}
};

}  // namespace elog

#endif  // __ELOG_EXPRESSION_H__