#ifndef __ELOG_EXPRESSION_PARSER_H__
#define __ELOG_EXPRESSION_PARSER_H__

#include "elog_expression.h"

namespace elog {

class ELogExpressionParser {
public:
    static ELogExpression* parseExpressionString(const char* exprStr);
};

}  // namespace elog

#endif  // __ELOG_EXPRESSION_PARSER_H__