#ifndef __ELOG_EXPRESSION_H__
#define __ELOG_EXPRESSION_H__

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

    /** @brief Operation expression type. */
    ET_OP_EXPR
};

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

}  // namespace elog

#endif  // __ELOG_EXPRESSION_H__